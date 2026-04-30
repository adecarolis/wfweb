// Serial transport: drives the rig directly over Web Serial CI-V.
// No C++ server in the loop for control. Audio is intentionally not handled
// here — Phase 2 routes audio through Web Audio + setSinkId.
//
// Design:
//   * Auto-detect — when the user picks a port, the transport probes a list
//     of common baud rates with a broadcast read-transceiver-ID frame. The
//     first baud that elicits a reply is the right one; the reply's "from"
//     byte is the rig's CI-V address. Detected baud + addr are cached in
//     localStorage; subsequent connects skip detection.
//   * Auto-reconnect — `tryAutoReconnect()` uses navigator.serial.getPorts()
//     to find ports the user has already paired with this origin and opens
//     the first one without showing a picker.
//   * Maintains a small JS rig-state cache (frequency, mode, filter, PTT,
//     S-meter) and synthesizes the JSON shapes the SPA already consumes
//     (rigInfo, status, update, meters) so handleMessage works unchanged.
//   * Outbound commands flow through a deduping FIFO keyed by command name,
//     mirroring cachingQueue::addUnique() semantics.
//   * Inbound CI-V frames flow through IcomCiv.CivParser; echoes (frames
//     whose "from" equals our controllerAddr) are filtered out.

(function (global) {
    'use strict';

    if (!global.RigTransport || !global.IcomCiv) {
        console.error('serial-transport.js: missing prerequisite modules');
        return;
    }

    var civ = global.IcomCiv;
    var RIG_CAPS = global.IcomRigCaps || {};

    // CI-V address → display name. The full table is generated from
    // rigs/*.rig by tools/extract-rig-caps.py and lives in IcomRigCaps;
    // we look up names from there. This minimal fallback covers the
    // common modern rigs in case rig-caps.js failed to load.
    function rigDisplayName(addr) {
        var entry = RIG_CAPS[addr];
        if (entry && entry.model) return entry.model;
        return 'Icom 0x' + addr.toString(16).toUpperCase();
    }

    // Bauds tried during auto-detect, ordered by likelihood. 19200 is the
    // most common Icom default; 115200 is what people set for snappy SDR
    // operation; 9600 is the original factory default for older rigs.
    var PROBE_BAUDS = [19200, 115200, 9600, 57600, 38400, 4800];
    var PROBE_TIMEOUT_MS = 500;

    // Substring patterns the rig's USB audio device usually advertises.
    // Used to identify the rig's audio interface among the OS audio devices
    // (PulseAudio/Pipewire labels them with the chip name on Linux, e.g.
    // "USB Audio CODEC" or "PCM2901"). Same pattern for input + output —
    // a USB audio gadget typically shows under both kinds.
    var RIG_AUDIO_PATTERN = /(USB Audio CODEC|USB-PCM Audio|IC-7300|IC-705|IC-9700|IC-7610|IC-7100|IC-9100|CP210|Burr-Brown|PCM29|PCM30)/i;

    // The SPA's populateModes() expects a flat array of mode-name strings —
    // mirrors what the C++ webserver sends.
    var DEFAULT_MODES = ['LSB', 'USB', 'AM', 'CW', 'RTTY', 'FM', 'CW-R', 'RTTY-R'];
    // populateFilters() takes [{num, name}, ...]
    var DEFAULT_FILTERS = [
        { num: 1, name: 'FIL1' }, { num: 2, name: 'FIL2' }, { num: 3, name: 'FIL3' },
    ];

    // Icom scope center-span table. Identical across IC-7300 / IC-705 /
    // IC-9700 (the modern HF/VHF rigs).
    var DEFAULT_SPANS = [
        { num: 0, name: '±2.5k', hz: 2500   },
        { num: 1, name: '±5k',   hz: 5000   },
        { num: 2, name: '±10k',  hz: 10000  },
        { num: 3, name: '±25k',  hz: 25000  },
        { num: 4, name: '±50k',  hz: 50000  },
        { num: 5, name: '±100k', hz: 100000 },
        { num: 6, name: '±250k', hz: 250000 },
    ];

    // Icom S-meter raw BCD (0..241) → dB relative to S9 (-54..+60), the
    // scale the SPA's drawSMeter() expects. Uses the per-rig table from
    // IcomRigCaps when available (rigs/IC-*.rig has per-model curves);
    // falls back to the IC-7300 default in icom.js for unknown rigs.
    function rawSMeterToDb(raw, civAddr) {
        var tbl = civ.getRigMeterTable(civAddr, 'sMeter');
        return civ.calMeter('sMeter', raw, tbl);
    }

    // SPA command name → 0x14 NN sub byte (analog levels 0..255)
    var ANALOG_SUB = {
        setAfGain:      0x01,
        setRfGain:      0x02,
        setSquelch:     0x03,
        setRfPower:     0x0A,
        setMicGain:     0x0B,
        setMonitorGain: 0x15,
        setPBTInner:    0x07,
        setPBTOuter:    0x08,
        setCWSpeed:     0x0C,
    };
    // 0x14 sub → field name reported to the SPA in 'update' messages.
    var ANALOG_SUB_TO_FIELD = {
        0x01: 'afGain',
        0x02: 'rfGain',
        0x03: 'squelch',
        0x0A: 'rfPower',
        0x0B: 'micGain',
        0x15: 'monitorGain',  // 0x0F is Break-in delay, NOT monitor — common confusion
        0x07: 'pbtInner',
        0x08: 'pbtOuter',
        0x0C: 'cwSpeed',
    };

    // SPA command name → 0x16 NN sub byte (bool toggles)
    var BOOL_SUB = {
        setAutoNotch:      0x41,
        setNoiseBlanker:   0x22,
        setNoiseReduction: 0x40,
        setMonitor:        0x45,
    };
    var BOOL_SUB_TO_FIELD = {
        0x41: 'autoNotch',
        0x22: 'nb',
        0x40: 'nr',
        0x45: 'monitor',
        0x02: 'preamp',  // multi-state but reply still parses through here
    };

    var HANDLED_CMDS = {
        // Already implemented
        getStatus: true, setFrequency: true, setMode: true,
        setFilter: true, setPTT: true,
        // Audio plumbing — Phase 2 will fill these in; for now silently drop
        // so they don't noisily fall back to a closed WS.
        enableMic: true, enableAudio: true,
        // Analog levels
        setAfGain: true, setRfGain: true, setRfPower: true, setSquelch: true,
        setMicGain: true, setMonitorGain: true, setPBTInner: true,
        setPBTOuter: true, setCWSpeed: true,
        // Bool toggles
        setAutoNotch: true, setNoiseBlanker: true, setNoiseReduction: true,
        setMonitor: true, setPreamp: true, setAttenuator: true,
        // VFO ops
        selectVFO: true, swapVFO: true, equalizeVFO: true, setSplit: true,
        // Misc
        setTuner: true, setPower: true, setSpan: true,
        // CW
        sendCW: true, stopCW: true,
        // Filter width / shape
        setFilterWidth: true,
        // Packet (WASM Direwolf modem; APRS UI-frame TX only for now).
        packetEnable: true, packetSetMode: true, aprsTxBeacon: true,
        // Anything else (FreeDV, memory, filter shape, LAN ops, reporters …)
        // falls through to the WS path which is closed in Direct mode.
    };

    function lsGetInt(key) {
        try { var v = localStorage.getItem(key); return v ? parseInt(v) : null; }
        catch (e) { return null; }
    }
    function lsSetInt(key, val) {
        try { localStorage.setItem(key, String(val)); } catch (e) { /* ignore */ }
    }

    // Decode a BCD-encoded byte (0x12 -> 12) — used for scope sequence numbers.
    function bcdByteToInt(b) {
        return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
    }

    class SerialRigTransport extends global.RigTransport {
        constructor(opts) {
            super();
            opts = opts || {};
            this.controllerAddr = opts.controllerAddr || 0xE0;
            this.onProgress = opts.onProgress || function () {};

            this.civAddr = lsGetInt('directCivAddr') || 0;       // 0 means "not yet detected"
            this.baudRate = lsGetInt('directBaudRate') || 0;

            this.parser = new civ.CivParser({
                controllerAddr: this.controllerAddr,
                onFrame: this._onFrame.bind(this),
            });

            this.port = null;
            this.writer = null;
            this.reader = null;
            this._open = false;
            this._connecting = false;

            this._queue = [];
            this._payloads = {};
            this._draining = false;

            this._sMeterTimer = null;
            this._safetyPollTimer = null;
            this._lastFreqStamp = 0;
            this._lastModeStamp = 0;

            this.state = {
                frequency: 0, mode: null, filter: null,
                sMeter: 0, transmitting: false,
            };

            // RX audio capture state — getUserMedia → AudioWorklet → Int16
            // PCM → 0x02 binary frame → SPA's handleAudioData.
            this._rxAudio = {
                ctx: null,        // AudioContext at 48kHz
                stream: null,     // MediaStream from getUserMedia
                source: null,     // MediaStreamAudioSourceNode
                node: null,       // AudioWorkletNode (capture)
                seq: 0,           // 0x02 frame sequence counter
                enabled: false,
            };

            // TX audio playback state — 0x03 frame → AudioWorklet ring buffer
            // → AudioContext destination routed via setSinkId() to the rig's
            // USB audio output. Lazy-initialised on first sendAudioFrame.
            this._txAudio = {
                ctx: null,
                node: null,         // AudioWorkletNode (ring buffer playback)
                deviceId: null,
                initPromise: null,  // dedupes concurrent _ensureTxAudioCtx() calls
            };

            // Saved Data-Off Mod Input value, captured before we flip the
            // rig to USB on enableMic so we can restore on disable.
            this._savedDataOffMod = null;

            // Per-rig MOD INPUT command bytes + USB/MIC register values.
            // Filled in once we identify the rig (default = IC-7300 layout
            // until we have a CI-V address).
            this._modIn = civ.getRigModInputs(this.civAddr);

            // Spectrum scope assembly state. The Icom scope command (0x27 0x00)
            // arrives as a sequence of frames: seq 1 carries header (mode +
            // start/end freq + out-of-range flag), seq 2..N carry pixels.
            this._scope = {
                seqMax: 0, mode: 0,
                startFreq: 0, endFreq: 0,
                pixels: [], capturing: false,
            };

            // Packet (Direwolf WASM) state. Lazy-loaded — we don't pay the
            // ~140 KB module download cost unless the user opens the packet
            // panel and toggles enable. modem stays alive across mode
            // changes; init() reconfigures it in place.
            this._packet = {
                enabled: false, mode: 1200, modem: null,
                modulePromise: null, txInFlight: false,
            };
        }

        // ----------------------------------------------------------------
        // RigTransport interface
        // ----------------------------------------------------------------

        async connect(prePicked) {
            if (this._open || this._connecting) return;
            this._connecting = true;
            try {
                var port = prePicked || (await navigator.serial.requestPort({}));
                await this._openWithDetection(port);
            } finally {
                this._connecting = false;
            }
        }

        // Opens the first previously-paired port without showing a picker.
        // Returns true if a port was opened, false if none paired.
        async tryAutoReconnect() {
            if (this._open || this._connecting) return true;
            if (!navigator.serial || !navigator.serial.getPorts) return false;
            var ports = await navigator.serial.getPorts();
            if (!ports || !ports.length) return false;
            this._connecting = true;
            try {
                await this._openWithDetection(ports[0]);
                return true;
            } catch (e) {
                console.warn('SerialRigTransport auto-reconnect failed:', e);
                return false;
            } finally {
                this._connecting = false;
            }
        }

        async close() {
            this._stopPolling();
            this._open = false;
            await this._stopRxAudio();
            await this._stopTxAudio();
            try { if (this.writer) this.writer.releaseLock(); } catch (e) { /* ignore */ }
            this.writer = null;
            try { if (this.reader) await this.reader.cancel(); } catch (e) { /* ignore */ }
            this.reader = null;
            try { if (this.port) await this.port.close(); } catch (e) { /* ignore */ }
            this.port = null;
            this._queue = [];
            this._payloads = {};
            this.dispatchEvent(new CustomEvent('close', { detail: { reason: 'user-close' } }));
        }

        isOpen() { return this._open; }

        sendCommand(obj) {
            if (!obj || !obj.cmd) return;

            // Analog levels — uniform 0x14 NN <BCD level> shape.
            if (ANALOG_SUB.hasOwnProperty(obj.cmd)) {
                var aSub = ANALOG_SUB[obj.cmd];
                // setCWSpeed uses obj.wpm; everything else uses obj.value.
                var aVal = (obj.cmd === 'setCWSpeed') ? obj.wpm : obj.value;
                if (typeof aVal !== 'number') return;
                var aField = ANALOG_SUB_TO_FIELD[aSub];
                this.state[aField] = aVal;
                this._enqueue(obj.cmd, civ.cmdSetAnalogLevel(aSub, aVal));
                var aUpd = {}; aUpd[aField] = aVal;
                this._emit('update', aUpd);
                return;
            }

            // Bool toggles — uniform 0x16 NN <0/1> shape.
            if (BOOL_SUB.hasOwnProperty(obj.cmd)) {
                var bSub = BOOL_SUB[obj.cmd];
                var bOn = !!obj.value;
                var bField = BOOL_SUB_TO_FIELD[bSub];
                this.state[bField] = bOn;
                this._enqueue(obj.cmd, civ.cmdSetBoolFunc(bSub, bOn));
                var bUpd = {}; bUpd[bField] = bOn;
                this._emit('update', bUpd);
                return;
            }

            switch (obj.cmd) {
                case 'getStatus':
                    this._emitFullStatus();
                    return;
                case 'setFrequency':
                    if (typeof obj.value !== 'number' || obj.value <= 0) return;
                    this.state.frequency = obj.value;
                    this._enqueue('setFreq', civ.cmdSetFrequency(obj.value));
                    this._emit('update', { frequency: obj.value, vfoAFrequency: obj.value });
                    return;
                case 'setMode':
                    if (typeof obj.value !== 'string') return;
                    this.state.mode = obj.value;
                    // Send mode WITHOUT a filter byte — cmdSetMode emits
                    // the bare 0x06 mode form, which makes the rig keep
                    // its per-mode remembered filter. Sending an explicit
                    // filter forces e.g. FIL1 every time something else
                    // changes the mode (packet.js auto-restoring its
                    // saved-enabled state on reload was clobbering the
                    // user's filter selection back to FIL1 every refresh).
                    this._enqueue('setMode', civ.cmdSetMode(obj.value, null));
                    this._emit('update', { mode: obj.value });
                    return;
                case 'setFilter':
                    if (typeof obj.value !== 'number') return;
                    this.state.filter = obj.value;
                    if (this.state.mode) {
                        this._enqueue('setMode', civ.cmdSetMode(this.state.mode, obj.value));
                    }
                    this._emit('update', { filter: obj.value });
                    return;
                case 'setFilterWidth':
                    if (typeof obj.value !== 'number' || obj.value <= 0) return;
                    this.state.filterWidth = obj.value;
                    var fwBytes = civ.cmdSetFilterWidth(obj.value, this.state.mode || 'USB');
                    console.log('setFilterWidth:', obj.value, 'Hz mode=', this.state.mode,
                        '→ bytes', Array.from(fwBytes).map(function(b){return b.toString(16).padStart(2,'0');}).join(' '));
                    this._enqueue('setFilterWidth', fwBytes);
                    this._emit('update', { filterWidth: obj.value });
                    return;
                case 'setPTT':
                    // Receiver-only rigs (R-series) won't accept PTT — drop
                    // the request silently rather than NAK the bus.
                    if (!this._rigCanTransmit()) return;
                    var ptt = !!obj.value;
                    this.state.transmitting = ptt;
                    this._enqueue('setPTT', civ.cmdSetPTT(ptt));
                    this._emit('update', { transmitting: ptt });
                    return;
                case 'setPreamp':
                    var pre = (typeof obj.value === 'number') ? obj.value : 0;
                    this.state.preamp = pre;
                    this._enqueue('setPreamp', civ.cmdSetPreamp(pre));
                    this._emit('update', { preamp: pre });
                    return;
                case 'setAttenuator':
                    var att = (typeof obj.value === 'number') ? obj.value : 0;
                    this.state.attenuator = att;
                    this._enqueue('setAttenuator', civ.cmdSetAttenuator(att));
                    this._emit('update', { attenuator: att });
                    return;
                case 'selectVFO':
                    var vfo = (obj.value === 'B') ? 'B' : 'A';
                    this._enqueue('selectVFO', civ.cmdSelectVFO(vfo));
                    this._emit('update', { selectedVfo: vfo });
                    return;
                case 'swapVFO':
                    this._enqueue('swapVFO', civ.cmdSwapVFO());
                    return;
                case 'equalizeVFO':
                    this._enqueue('equalizeVFO', civ.cmdEqualizeVFO());
                    return;
                case 'setSplit':
                    var sp = !!obj.value;
                    this.state.split = sp;
                    this._enqueue('setSplit', civ.cmdSetSplit(sp));
                    this._emit('update', { split: sp });
                    return;
                case 'setTuner':
                    var tn = (typeof obj.value === 'number') ? obj.value : 0;
                    this.state.tuner = tn;
                    this._enqueue('setTuner', civ.cmdSetTuner(tn));
                    this._emit('update', { tuner: tn });
                    return;
                case 'setPower':
                    this._enqueue('setPower', civ.cmdSetPower(!!obj.value));
                    return;
                case 'setSpan':
                    var spIdx = (typeof obj.value === 'number') ? obj.value : -1;
                    if (spIdx >= 0 && spIdx < DEFAULT_SPANS.length) {
                        var spHz = DEFAULT_SPANS[spIdx].hz;
                        this.state.spanIndex = spIdx;
                        this._enqueue('setSpan', civ.cmdSetScopeSpan(spHz));
                        this._emit('update', { spanIndex: spIdx });
                    }
                    return;
                case 'sendCW':
                    if (!this._rigCanTransmit()) return;
                    if (typeof obj.text !== 'string' || !obj.text.length) return;
                    this._enqueue('sendCW', civ.cmdSendCW(obj.text));
                    return;
                case 'stopCW':
                    if (!this._rigCanTransmit()) return;
                    this._enqueue('stopCW', civ.cmdStopCW());
                    return;
                case 'packetEnable':
                    if (!this._rigCanTransmit()) return;
                    this._packetSetEnabled(!!obj.value);
                    return;
                case 'packetSetMode':
                    if (!this._rigCanTransmit()) return;
                    this._packetSetMode(obj.value | 0);
                    return;
                case 'aprsTxBeacon':
                    if (!this._rigCanTransmit()) return;
                    this._packetTxBeacon(obj);
                    return;
                case 'enableAudio':
                    if (obj.value) {
                        this._startRxAudio().catch(function (err) {
                            console.error('SerialRigTransport: enableAudio failed:', err);
                        });
                    } else {
                        this._stopRxAudio().catch(function () {});
                    }
                    return;
                case 'enableMic':
                    // Switch the rig's modulation input to USB so it
                    // accepts the audio we're streaming over the USB
                    // audio interface (otherwise SSB-voice TX is silent
                    // because the rig is listening to the front-panel
                    // MIC). Restore the previous setting on mic-off.
                    if (!this._rigCanTransmit()) return;
                    if (obj.value) {
                        this._enqueue('setDataOffMod',
                            civ.cmdSetModInput(this._modIn.off, this._modIn.usbReg));
                    } else if (this._savedDataOffMod !== null) {
                        this._enqueue('setDataOffMod',
                            civ.cmdSetModInput(this._modIn.off, this._savedDataOffMod));
                    }
                    return;
                default:
                    return;
            }
        }

        sendAudioFrame(buffer) {
            // Frame format: [0x03][0x00][seq u16LE][rsv u16LE][PCM Int16 LE]
            if (!buffer || buffer.byteLength < 8) return;
            var view = new DataView(buffer);
            if (view.getUint8(0) !== 0x03) return;

            // Lazy-init the TX context the first time we get audio. The
            // SPA fires this from a user gesture (PTT click, FT8 TX click,
            // mic click) so AudioContext + setSinkId calls are allowed.
            var self = this;
            if (!this._txAudio.ctx) {
                this._ensureTxAudioCtx().catch(function (e) {
                    console.error('SerialRigTransport: TX audio init failed:', e);
                });
                return;  // drop this chunk; subsequent chunks will play
            }
            if (!this._txAudio.node) return;

            // Convert Int16 PCM to Float32 [-1..1] for the playback worklet.
            var int16 = new Int16Array(buffer, 6);
            var f32 = new Float32Array(int16.length);
            for (var i = 0; i < int16.length; i++) f32[i] = int16[i] / 32768;
            try {
                this._txAudio.node.port.postMessage(f32);
            } catch (e) {
                /* node may have been closed mid-call */
            }
        }

        async _ensureTxAudioCtx() {
            if (this._txAudio.ctx) return;
            if (this._txAudio.initPromise) return this._txAudio.initPromise;
            var self = this;
            this._txAudio.initPromise = (async function () {
                var ctx = new AudioContext({ sampleRate: 48000 });

                // Pick the rig USB audio output device. Use a saved id if
                // available; otherwise look for a label that matches the
                // rig family. The user must have already granted audio
                // permission via RX audio for labels to be readable.
                var deviceId = null;
                try { deviceId = localStorage.getItem('directTxAudioId'); } catch (e) { /* ignore */ }
                if (!deviceId) {
                    try {
                        var devices = await navigator.mediaDevices.enumerateDevices();
                        for (var i = 0; i < devices.length; i++) {
                            var d = devices[i];
                            if (d.kind === 'audiooutput' && RIG_AUDIO_PATTERN.test(d.label)) {
                                deviceId = d.deviceId;
                                console.log('SerialRigTransport: using TX audio device:', d.label);
                                try { localStorage.setItem('directTxAudioId', deviceId); } catch (e) { /* ignore */ }
                                break;
                            }
                        }
                    } catch (e) {
                        console.warn('Could not enumerate audio devices for TX:', e);
                    }
                }
                self._txAudio.deviceId = deviceId;

                if (deviceId && typeof ctx.setSinkId === 'function') {
                    try {
                        await ctx.setSinkId(deviceId);
                    } catch (e) {
                        console.warn('AudioContext.setSinkId failed:', e);
                    }
                }
                // Chromium creates AudioContexts in 'suspended' state until a
                // user gesture; without resume() the worklet never runs and
                // the rig hears silence. PTT firing in FT8 (control-plane
                // gesture) doesn't auto-resume an unrelated context.
                if (ctx.state === 'suspended') {
                    try { await ctx.resume(); } catch (e) {
                        console.warn('AudioContext.resume failed:', e);
                    }
                }

                // Playback worklet: ring buffer fed by main-thread postMessage.
                // ~1.5 s capacity is plenty for a TX queue.
                var workletCode =
                    'class TxPlayback extends AudioWorkletProcessor {' +
                    '  constructor() {' +
                    '    super();' +
                    '    this.buffer = new Float32Array(sampleRate * 1.5);' +
                    '    this.writePos = 0; this.readPos = 0; this.buffered = 0;' +
                    '    this.port.onmessage = (e) => {' +
                    '      var s = e.data; var len = s.length; var bufLen = this.buffer.length;' +
                    '      for (var i = 0; i < len; i++) { this.buffer[this.writePos] = s[i]; this.writePos = (this.writePos + 1) % bufLen; }' +
                    '      this.buffered += len; if (this.buffered > bufLen) this.buffered = bufLen;' +
                    '    };' +
                    '  }' +
                    '  process(inputs, outputs) {' +
                    '    var output = outputs[0][0]; if (!output) return true;' +
                    '    var bufLen = this.buffer.length;' +
                    '    for (var i = 0; i < output.length; i++) {' +
                    '      if (this.buffered > 0) { output[i] = this.buffer[this.readPos]; this.readPos = (this.readPos + 1) % bufLen; this.buffered--; }' +
                    '      else { output[i] = 0; }' +
                    '    }' +
                    '    return true;' +
                    '  }' +
                    '}' +
                    'registerProcessor("wfweb-tx-playback", TxPlayback);';
                var url = URL.createObjectURL(new Blob([workletCode], { type: 'application/javascript' }));
                await ctx.audioWorklet.addModule(url);
                URL.revokeObjectURL(url);

                var node = new AudioWorkletNode(ctx, 'wfweb-tx-playback');
                node.connect(ctx.destination);

                self._txAudio.ctx = ctx;
                self._txAudio.node = node;
                console.log('SerialRigTransport: TX audio ready — sinkId=' +
                    (ctx.sinkId || '(default)') + ', deviceId=' +
                    (deviceId ? deviceId.slice(0, 12) + '…' : '(none)') +
                    ', state=' + ctx.state);
            })();
            // Clear the cached promise on failure so the next call (which
            // will hopefully arrive from a real user gesture) gets a fresh
            // attempt instead of awaiting a permanently-rejected promise.
            this._txAudio.initPromise.catch(function () {
                if (!self._txAudio.ctx) self._txAudio.initPromise = null;
            });
            return this._txAudio.initPromise;
        }

        async _stopTxAudio() {
            if (this._txAudio.node) {
                try { this._txAudio.node.disconnect(); } catch (e) { /* ignore */ }
                this._txAudio.node = null;
            }
            if (this._txAudio.ctx) {
                try { await this._txAudio.ctx.close(); } catch (e) { /* ignore */ }
                this._txAudio.ctx = null;
            }
            this._txAudio.initPromise = null;
        }

        // ----------------------------------------------------------------
        // Detection + open
        // ----------------------------------------------------------------

        async _openWithDetection(port) {
            // Optimistic path: we have saved baud/addr from a previous
            // successful connect — try those first.
            var savedBaud = this.baudRate;
            var savedAddr = this.civAddr;

            if (savedBaud) {
                this.onProgress({ stage: 'opening', baud: savedBaud });
                try {
                    await port.open({
                        baudRate: savedBaud, dataBits: 8, stopBits: 1,
                        parity: 'none', flowControl: 'none',
                    });
                } catch (e) {
                    throw new Error('Could not open serial port (' +
                        (e && e.message ? e.message : e) + '). Is another program using it?');
                }
                var addr = await this._probePort(port, PROBE_TIMEOUT_MS);
                if (addr) {
                    this._finalizeDetection(port, savedBaud, addr);
                    return;
                }
                // Saved baud didn't elicit a reply — fall through to full probe.
                try { await port.close(); } catch (e) { /* ignore */ }
            }

            // Full auto-detect across PROBE_BAUDS. Saved baud (if any) tried
            // first since the user almost certainly hasn't changed it.
            var bauds = savedBaud
                ? [savedBaud].concat(PROBE_BAUDS.filter(function (b) { return b !== savedBaud; }))
                : PROBE_BAUDS.slice();

            for (var i = 0; i < bauds.length; i++) {
                var baud = bauds[i];
                this.onProgress({ stage: 'probing', baud: baud, attempt: i + 1, total: bauds.length });
                try {
                    await port.open({
                        baudRate: baud, dataBits: 8, stopBits: 1,
                        parity: 'none', flowControl: 'none',
                    });
                } catch (e) {
                    if (i === 0) {
                        throw new Error('Could not open serial port (' +
                            (e && e.message ? e.message : e) + '). Is another program using it?');
                    }
                    continue;
                }
                var found = await this._probePort(port, PROBE_TIMEOUT_MS);
                if (found) {
                    this._finalizeDetection(port, baud, found);
                    return;
                }
                try { await port.close(); } catch (e) { /* ignore */ }
            }

            throw new Error('No CI-V reply at any common baud rate. ' +
                'Check the radio is on, CI-V is enabled, and the cable is connected to the correct port.');
        }

        // Sends a broadcast read-TX-ID on an already-open port and listens
        // up to timeoutMs for a reply. Returns the rig's CI-V address (the
        // 'from' byte of the first reply frame), or null on timeout.
        async _probePort(port, timeoutMs) {
            if (!port) return null;
            // Send probe frame
            var writer = port.writable.getWriter();
            try {
                await writer.write(civ.PROBE_FRAME);
            } catch (e) {
                writer.releaseLock();
                return null;
            }
            writer.releaseLock();

            // Listen until timeout or first non-echo frame.
            var localParser = new civ.CivParser({ controllerAddr: this.controllerAddr });
            var detected = null;
            localParser.onFrame = function (frame) {
                if (!detected && frame.payload && frame.payload[0] === 0x19) {
                    detected = frame.from;
                }
            };

            var reader = port.readable.getReader();
            var aborted = false;
            var timer = setTimeout(function () {
                aborted = true;
                reader.cancel().catch(function () {});
            }, timeoutMs);

            try {
                while (!detected && !aborted) {
                    var result = await reader.read();
                    if (result.done) break;
                    if (result.value && result.value.length) localParser.feed(result.value);
                }
            } catch (e) { /* expected on cancel */ }

            clearTimeout(timer);
            try { reader.releaseLock(); } catch (e) { /* ignore */ }
            return detected;
        }

        _finalizeDetection(port, baud, addr) {
            this.baudRate = baud;
            this.civAddr = addr;
            this._modIn = civ.getRigModInputs(addr);
            lsSetInt('directBaudRate', baud);
            lsSetInt('directCivAddr', addr);

            this.port = port;
            this.writer = port.writable.getWriter();
            this._open = true;

            this.onProgress({
                stage: 'connected', baud: baud, civAddr: addr,
                model: rigDisplayName(addr),
            });

            this._readLoop().catch((err) => {
                console.error('SerialRigTransport read loop ended:', err);
                this._notifyClose(err);
            });

            this.dispatchEvent(new Event('open'));
            this._emitRigInfo();

            // Initial reads — freq / mode / ptt / s-meter.
            this._enqueue('readFreq',   civ.cmdReadFrequency());
            this._enqueue('readMode',   civ.cmdReadMode());
            this._enqueue('readPTT',    civ.cmdReadPTT());
            this._enqueue('readSMeter', civ.cmdReadSMeter());

            // Initial reads — analog levels (so the UI sliders show the
            // rig's current values rather than zeros).
            for (var cmdName in ANALOG_SUB) {
                if (cmdName === 'setCWSpeed') continue;  // CW speed is rare; skip
                var sub = ANALOG_SUB[cmdName];
                this._enqueue('read_' + cmdName, civ.cmdReadAnalogLevel(sub));
            }

            // Initial reads — bool toggles + multi-state preamp.
            for (var cmdName2 in BOOL_SUB) {
                this._enqueue('read_' + cmdName2, civ.cmdReadBoolFunc(BOOL_SUB[cmdName2]));
            }
            this._enqueue('readPreamp',     civ.cmdReadBoolFunc(0x02));
            this._enqueue('readAttenuator', civ.cmdReadAttenuator());
            this._enqueue('readSplit',      civ.cmdReadSplit());
            this._enqueue('readTuner',      civ.cmdReadTuner());
            this._enqueue('readScopeSpan',  civ.cmdReadScopeSpan());
            this._enqueue('readDataOffMod', civ.cmdReadModInput(this._modIn.off));
            this._enqueue('readFilterWidth', civ.cmdReadFilterWidth());

            // Enable scope output (waterfall). Single-byte payloads match
            // the C++ wfweb's behaviour for single-receiver rigs.
            this._enqueue('scopeOn',   new Uint8Array([0x27, 0x10, 0x01]));
            this._enqueue('scopeData', new Uint8Array([0x27, 0x11, 0x01]));

            this._startPolling();
        }

        // ----------------------------------------------------------------
        // Internals (queue, read loop, parsing, polling)
        // ----------------------------------------------------------------

        _enqueue(key, payload) {
            this._payloads[key] = payload;
            if (this._queue.indexOf(key) === -1) this._queue.push(key);
            this._drain();
        }

        async _drain() {
            if (this._draining || !this._open) return;
            this._draining = true;
            try {
                while (this._queue.length > 0 && this._open && this.writer) {
                    var key = this._queue.shift();
                    var payload = this._payloads[key];
                    delete this._payloads[key];
                    if (!payload) continue;
                    var frame = civ.buildFrame(this.civAddr, this.controllerAddr, payload);
                    await this.writer.write(frame);
                    await new Promise(function (r) { setTimeout(r, 5); });
                }
            } catch (err) {
                console.error('SerialRigTransport drain error:', err);
                this._notifyClose(err);
            } finally {
                this._draining = false;
            }
        }

        async _readLoop() {
            var port = this.port;
            if (!port || !port.readable) return;
            while (this._open && port.readable) {
                var reader = port.readable.getReader();
                this.reader = reader;
                try {
                    while (true) {
                        var res = await reader.read();
                        if (res.done) break;
                        if (res.value && res.value.length) this.parser.feed(res.value);
                    }
                } finally {
                    try { reader.releaseLock(); } catch (e) { /* ignore */ }
                    this.reader = null;
                }
                if (!this._open) break;
                await new Promise(function (r) { setTimeout(r, 50); });
            }
        }

        _onFrame(frame) {
            var payload = frame.payload;
            if (!payload || payload.length === 0) return;

            // Spectrum scope (0x27 0x00) — assembled across multiple frames,
            // emitted to the SPA as a binary 0x01 message when complete.
            if (payload.length >= 2 && payload[0] === 0x27 && payload[1] === 0x00) {
                this._handleScopeFrame(payload);
                return;
            }

            // Scope center-span (0x27 0x15) — single frame, used to surface
            // the rig's current span to the SPA's SPAN button.
            if (payload.length >= 6 && payload[0] === 0x27 && payload[1] === 0x15) {
                var spanHz = civ.parseScopeSpanReply(payload);
                if (spanHz !== null) {
                    var idx = -1;
                    for (var k = 0; k < DEFAULT_SPANS.length; k++) {
                        if (DEFAULT_SPANS[k].hz === spanHz) { idx = k; break; }
                    }
                    if (idx >= 0 && this.state.spanIndex !== idx) {
                        this.state.spanIndex = idx;
                        this._emit('update', { spanIndex: idx });
                    }
                }
                return;
            }

            var freqHz = civ.parseFrequencyReply(payload);
            if (freqHz !== null && freqHz > 0) {
                this._lastFreqStamp = Date.now();
                if (freqHz !== this.state.frequency) {
                    this.state.frequency = freqHz;
                    this._emit('update', { frequency: freqHz, vfoAFrequency: freqHz });
                }
                return;
            }

            var modeReply = civ.parseModeReply(payload);
            if (modeReply !== null) {
                this._lastModeStamp = Date.now();
                var modeChanged = (modeReply.mode !== this.state.mode);
                var filterChanged = (modeReply.filter !== null && modeReply.filter !== this.state.filter);
                if (modeChanged) this.state.mode = modeReply.mode;
                if (filterChanged) this.state.filter = modeReply.filter;
                if (modeChanged || filterChanged) {
                    var update = {};
                    if (modeChanged) update.mode = modeReply.mode;
                    if (filterChanged) update.filter = modeReply.filter;
                    this._emit('update', update);
                }
                return;
            }

            var pttState = civ.parsePTTReply(payload);
            if (pttState !== null) {
                if (pttState !== this.state.transmitting) {
                    this.state.transmitting = pttState;
                    this._emit('update', { transmitting: pttState });
                }
                return;
            }

            var sMeterRaw = civ.parseSMeterReply(payload);
            if (sMeterRaw !== null) {
                var sMeterDb = rawSMeterToDb(sMeterRaw, this.civAddr);
                if (sMeterDb !== this.state.sMeter) {
                    this.state.sMeter = sMeterDb;
                    this._emit('meters', { sMeter: sMeterDb });
                }
                return;
            }

            // 0x14 NN — analog level reply (afGain / rfGain / squelch / …)
            if (payload[0] === 0x14 && payload.length >= 4) {
                var aReply = civ.parseAnalogLevelReply(payload);
                if (aReply) {
                    var aField = ANALOG_SUB_TO_FIELD[aReply.sub];
                    if (aField && this.state[aField] !== aReply.value) {
                        this.state[aField] = aReply.value;
                        var u = {}; u[aField] = aReply.value;
                        this._emit('update', u);
                    }
                    return;
                }
            }

            // 0x15 NN — TX meters (power 0x11 / SWR 0x12 / ALC 0x13)
            if (payload[0] === 0x15 && payload.length >= 4) {
                var sub = payload[1];
                var meterField = (sub === 0x11) ? 'powerMeter'
                                : (sub === 0x12) ? 'swrMeter'
                                : (sub === 0x13) ? 'alcMeter' : null;
                if (meterField) {
                    var raw = civ.parseTxMeterReply(payload, sub);
                    if (raw !== null) {
                        var calKind = (sub === 0x11) ? 'power'
                                    : (sub === 0x12) ? 'swr' : 'alc';
                        var calTbl = civ.getRigMeterTable(this.civAddr, calKind);
                        var v = civ.calMeter(calKind, raw, calTbl);
                        if (this.state[meterField] !== v) {
                            this.state[meterField] = v;
                            var m = {}; m[meterField] = v;
                            this._emit('meters', m);
                        }
                    }
                    return;
                }
            }

            // 0x16 NN — bool toggle reply (NB / NR / ANF / preamp)
            if (payload[0] === 0x16 && payload.length >= 3) {
                var bReply = civ.parseBoolFuncReply(payload);
                if (bReply) {
                    var bField = BOOL_SUB_TO_FIELD[bReply.sub];
                    if (bField) {
                        var bVal = (bReply.sub === 0x02) ? bReply.value : !!bReply.value;
                        if (this.state[bField] !== bVal) {
                            this.state[bField] = bVal;
                            var bu = {}; bu[bField] = bVal;
                            this._emit('update', bu);
                        }
                    }
                    return;
                }
            }

            // 0x11 — attenuator reply
            if (payload[0] === 0x11 && payload.length >= 2) {
                var att = civ.parseAttenuatorReply(payload);
                if (att !== null && this.state.attenuator !== att) {
                    this.state.attenuator = att;
                    this._emit('update', { attenuator: att });
                }
                return;
            }

            // 0x0F — split status reply
            if (payload[0] === 0x0F && payload.length >= 2) {
                var sp = civ.parseSplitReply(payload);
                if (sp !== null && this.state.split !== sp) {
                    this.state.split = sp;
                    this._emit('update', { split: sp });
                }
                return;
            }

            // 0x1C 0x01 — tuner status reply
            if (payload[0] === 0x1C && payload[1] === 0x01 && payload.length >= 3) {
                var tn = civ.parseTunerReply(payload);
                if (tn !== null && this.state.tuner !== tn) {
                    this.state.tuner = tn;
                    this._emit('update', { tuner: tn });
                }
                return;
            }

            // 0x1A 0x05 0x00 0x66 — Data Off Mod Input reply.
            // Cache the rig's current value so we can restore it after the
            // user toggles mic off. Prefix bytes are rig-specific.
            var modOffPrefix = this._modIn.off;
            var modOffMatch = payload.length >= modOffPrefix.length + 1;
            for (var mi = 0; mi < modOffPrefix.length && modOffMatch; mi++) {
                if (payload[mi] !== modOffPrefix[mi]) modOffMatch = false;
            }
            if (modOffMatch) {
                var v = civ.parseModInputReply(payload, modOffPrefix);
                // Don't overwrite our saved value with our own write-back
                // (when rig echoes back USB after we set it). Keep the
                // first-seen value as the user's baseline.
                if (v !== null && this._savedDataOffMod === null && v !== this._modIn.usbReg) {
                    this._savedDataOffMod = v;
                }
                return;
            }

            // 0x1A 0x03 — Filter Width reply (encoding depends on current mode).
            if (payload.length >= 3 && payload[0] === 0x1A && payload[1] === 0x03) {
                var hz = civ.parseFilterWidthReply(payload, this.state.mode || 'USB');
                console.log('parseFilterWidth:',
                    Array.from(payload).map(function(b){return b.toString(16).padStart(2,'0');}).join(' '),
                    'mode=', this.state.mode, '→', hz, 'Hz');
                if (hz !== null && hz !== this.state.filterWidth) {
                    this.state.filterWidth = hz;
                    this._emit('update', { filterWidth: hz });
                }
                return;
            }

            if (civ.parseAck(payload) !== null) return;
        }

        _emit(type, fields) {
            var msg = Object.assign({ type: type }, fields);
            this.dispatchEvent(new CustomEvent('message', { detail: msg }));
        }

        // Layout of a 0x27 0x00 frame after CivParser strips FE FE / FD:
        //   payload[0..1]   = 0x27 0x00 (cmd + sub)
        //   payload[2]      = receiver index (0 for single-receiver rigs)
        //   payload[3]      = sequence number       (BCD-encoded byte)
        //   payload[4]      = sequence max (= total #frames in this scope dump)
        // For seq 1:
        //   payload[5]      = scope mode (0=center, 1=fixed, 2=scroll-C, 3=scroll-F)
        //   payload[6..10]  = start freq, 5 BCD-LE bytes (Hz)
        //   payload[11..15] = end freq,   5 BCD-LE bytes (Hz)
        //   payload[16]     = out-of-range flag
        // For seq 2..(seqMax-1): 50 pixels at payload[5..]
        // For seq seqMax:        final ~25 pixels at payload[5..]
        _handleScopeFrame(payload) {
            if (payload.length < 5) return;
            var seq = bcdByteToInt(payload[3]);
            var seqMax = bcdByteToInt(payload[4]);

            if (seq === 1) {
                if (payload.length < 17) return;
                var mode = payload[5];
                var startFreq = civ.decodeBcdLE(payload.slice(6, 11));
                var endFreq   = civ.decodeBcdLE(payload.slice(11, 16));
                var oor = payload[16];
                if (mode === 0) {
                    // Center mode: rig sends center + half-span. Convert to
                    // (start, end) so the SPA's edge labels are correct.
                    var halfSpan = endFreq;
                    startFreq = startFreq - halfSpan;
                    endFreq   = startFreq + 2 * halfSpan;
                }
                this._scope = {
                    seqMax: seqMax, mode: mode,
                    startFreq: startFreq, endFreq: endFreq,
                    pixels: [], capturing: !oor,
                };
                if (oor) this._emitSpectrum(true);
                return;
            }

            if (!this._scope.capturing) return;
            // Pixel chunk: payload[5..end]
            for (var i = 5; i < payload.length; i++) this._scope.pixels.push(payload[i]);
            if (seq === this._scope.seqMax) {
                this._emitSpectrum(false);
                this._scope.capturing = false;
            }
        }

        _emitSpectrum(empty) {
            var pixels = empty ? [] : this._scope.pixels;
            var startMhz = this._scope.startFreq / 1e6;
            var endMhz   = this._scope.endFreq / 1e6;
            var buf = new ArrayBuffer(12 + pixels.length);
            var view = new DataView(buf);
            view.setUint8(0, 0x01);
            view.setUint8(1, 0x00);
            view.setUint16(2, 0, true);
            view.setFloat32(4, startMhz, true);
            view.setFloat32(8, endMhz,   true);
            if (pixels.length) {
                var bytes = new Uint8Array(buf, 12);
                for (var i = 0; i < pixels.length; i++) bytes[i] = pixels[i];
            }
            this.dispatchEvent(new CustomEvent('binary', { detail: buf }));
        }

        // Whether the connected rig accepts TX commands. Receivers (R-series
        // — IC-R7100/R8500/R8600) have hasTransmit=false in rig-caps; the
        // SPA hides the TX UI but a stale rigInfo or buggy caller could
        // still try, so we gate at the transport too.
        _rigCanTransmit() {
            var entry = RIG_CAPS[this.civAddr];
            if (!entry || !entry.caps) return true;  // unknown rig: assume TX
            return entry.caps.hasTransmit !== false;
        }

        _emitRigInfo() {
            // Pull per-rig caps from the auto-generated rig-caps registry.
            // Falls back to "modern HF rig with scope and TX" when the rig
            // isn't in the registry (preserves prior behaviour for unknown
            // CI-V addresses on the bus).
            var entry = RIG_CAPS[this.civAddr];
            var caps = (entry && entry.caps) || {
                hasTransmit: true, hasSpectrum: true, hasLAN: false,
                numReceivers: 1, numVFOs: 2,
            };
            this._emit('rigInfo', {
                version: '0.6.1-direct',
                model: rigDisplayName(this.civAddr),
                connected: true,
                modes: DEFAULT_MODES,
                filters: DEFAULT_FILTERS,
                spans: DEFAULT_SPANS,
                hasFilterSettings: true,
                hasMainSub: caps.numReceivers > 1,
                hasSpectrum: caps.hasSpectrum,
                spectAmpMax: 160,     // Icom amplitude scale (matches C++ wfweb)
                audioAvailable: true,    // Phase 2: rig USB audio via getUserMedia
                audioSampleRate: 48000,
                txAudioAvailable: caps.hasTransmit,
                hasPowerControl: false,
                hasTransmit: caps.hasTransmit,
                hasLAN: caps.hasLAN,
                numReceivers: caps.numReceivers,
                numVFOs: caps.numVFOs,
            });
        }

        _emitFullStatus() {
            var s = {
                frequency: this.state.frequency,
                vfoAFrequency: this.state.frequency,
                selectedVfo: 'A',
                mode: this.state.mode,
                filter: this.state.filter,
                sMeter: this.state.sMeter,
                transmitting: this.state.transmitting,
            };
            // Pass through any cached values so the UI populates fully.
            var passthrough = ['afGain', 'rfGain', 'rfPower', 'squelch',
                'micGain', 'monitorGain', 'pbtInner', 'pbtOuter', 'cwSpeed',
                'autoNotch', 'nb', 'nr', 'preamp', 'attenuator',
                'split', 'tuner', 'spanIndex', 'filterWidth'];
            for (var i = 0; i < passthrough.length; i++) {
                var k = passthrough[i];
                if (this.state[k] !== undefined && this.state[k] !== null) s[k] = this.state[k];
            }
            this._emit('status', s);
        }

        _startPolling() {
            // RX: S-meter every 200ms while not transmitting.
            // TX: power, SWR, ALC every 200ms while transmitting.
            this._sMeterTimer = setInterval(() => {
                if (!this._open) return;
                if (this.state.transmitting) {
                    this._enqueue('readPowerMeter', civ.cmdReadPowerMeter());
                    this._enqueue('readSwrMeter',   civ.cmdReadSwrMeter());
                    this._enqueue('readAlcMeter',   civ.cmdReadAlcMeter());
                } else {
                    this._enqueue('readSMeter', civ.cmdReadSMeter());
                }
            }, 200);
            this._safetyPollTimer = setInterval(() => {
                if (!this._open) return;
                var now = Date.now();
                if (now - this._lastFreqStamp > 2000) this._enqueue('readFreq', civ.cmdReadFrequency());
                if (now - this._lastModeStamp > 2000) this._enqueue('readMode', civ.cmdReadMode());
            }, 1000);
        }

        _stopPolling() {
            if (this._sMeterTimer) { clearInterval(this._sMeterTimer); this._sMeterTimer = null; }
            if (this._safetyPollTimer) { clearInterval(this._safetyPollTimer); this._safetyPollTimer = null; }
        }

        _notifyClose(reason) {
            if (!this._open) return;
            this._open = false;
            this._stopPolling();
            this._stopRxAudio().catch(function () {});
            this._stopTxAudio().catch(function () {});
            this.dispatchEvent(new CustomEvent('close', { detail: { reason: reason } }));
        }

        // ----------------------------------------------------------------
        // RX audio (Phase 2): rig USB audio → AudioWorklet → 0x02 frames
        // ----------------------------------------------------------------

        async _startRxAudio() {
            if (this._rxAudio.enabled) return;
            this._rxAudio.enabled = true;

            var stream = await this._openRxStream();
            this._rxAudio.stream = stream;

            // AudioContext at 48kHz to match what we tell the SPA in rigInfo.
            // The browser resamples the device's native rate as needed.
            var ctx = new AudioContext({ sampleRate: 48000 });
            this._rxAudio.ctx = ctx;

            // Inline AudioWorklet processor — copies Float32 input to main
            // thread. The SPA does the rest of the work (Int16 conversion is
            // done here, then handed off to handleAudioData via 0x02 frame).
            var workletCode =
                'class RxCapture extends AudioWorkletProcessor {' +
                '  process(inputs) {' +
                '    var i = inputs[0];' +
                '    if (i && i[0]) this.port.postMessage(i[0].slice());' +
                '    return true;' +
                '  }' +
                '}' +
                'registerProcessor("wfweb-rx-capture", RxCapture);';
            var url = URL.createObjectURL(new Blob([workletCode], { type: 'application/javascript' }));
            await ctx.audioWorklet.addModule(url);
            URL.revokeObjectURL(url);

            var src = ctx.createMediaStreamSource(stream);
            var node = new AudioWorkletNode(ctx, 'wfweb-rx-capture');
            src.connect(node);
            // Intentionally no connect to ctx.destination — this context
            // is capture-only; the SPA's playback path owns the speakers.

            this._rxAudio.source = src;
            this._rxAudio.node = node;

            // Tell the SPA audio is now enabled so handleAudioData() starts
            // accepting our synthesized 0x02 frames. Without this, audioEnabled
            // stays false and every incoming frame is dropped at the top of
            // handleAudioData. dispatchEvent runs the SPA listener
            // synchronously, so audioEnabled is true before we set up
            // port.onmessage and the first sample chunk flows.
            this._emit('audioStatus', { enabled: true, sampleRate: 48000 });

            var self = this;
            node.port.onmessage = function (e) {
                if (!self._rxAudio.enabled) return;
                var f32 = e.data;
                var int16 = new Int16Array(f32.length);
                for (var i = 0; i < f32.length; i++) {
                    var s = f32[i];
                    if (s > 1) s = 1; else if (s < -1) s = -1;
                    int16[i] = (s * 32767) | 0;
                }
                // Binary 0x02 frame format: [0x02][0x00][seq u16LE][rsv u16LE][PCM Int16 LE]
                var buf = new ArrayBuffer(6 + int16.byteLength);
                var view = new DataView(buf);
                view.setUint8(0, 0x02);
                view.setUint8(1, 0x00);
                view.setUint16(2, (self._rxAudio.seq++) & 0xFFFF, true);
                view.setUint16(4, 0, true);
                new Int16Array(buf, 6).set(int16);
                self.dispatchEvent(new CustomEvent('binary', { detail: buf }));

                // Tee a copy into the packet modem if it's running. Same
                // 48 kHz Int16 samples; modem.processSamples copies into
                // the WASM heap before invoking the demod.
                if (self._packet && self._packet.enabled && self._packet.modem) {
                    try { self._packet.modem.processSamples(int16); }
                    catch (err) { console.warn('packet RX feed:', err); }
                }
            };
        }

        async _openRxStream() {
            var constraints = {
                echoCancellation: false,
                noiseSuppression: false,
                autoGainControl: false,
            };

            // Step 1 — find the rig's audio input by label match.
            // enumerateDevices() returns labels only after the user has
            // already granted audio permission to this origin. If labels
            // are missing we acquire permission once with the default mic
            // (immediately stopped) so the next enumerate has labels.
            var devices = await navigator.mediaDevices.enumerateDevices();
            var hasLabels = devices.some(function (d) {
                return d.kind === 'audioinput' && d.label;
            });
            if (!hasLabels) {
                var primer = await navigator.mediaDevices.getUserMedia({ audio: constraints });
                try { primer.getTracks().forEach(function (t) { t.stop(); }); } catch (e) { /* ignore */ }
                devices = await navigator.mediaDevices.enumerateDevices();
            }

            var rigDev = null;
            for (var i = 0; i < devices.length; i++) {
                var d = devices[i];
                if (d.kind === 'audioinput' && RIG_AUDIO_PATTERN.test(d.label)) {
                    rigDev = d;
                    break;
                }
            }

            // Step 2 — validate / refresh the saved deviceId. If it no
            // longer points at a rig-labelled device, drop it. The early
            // bug here was: a stale id pointing at the laptop mic caused
            // permanent loopback (mic → 0x02 frames → speakers).
            var savedId = null;
            try { savedId = localStorage.getItem('directRxAudioId'); } catch (e) { /* ignore */ }
            if (savedId) {
                var match = devices.find(function (x) {
                    return x.kind === 'audioinput' && x.deviceId === savedId;
                });
                if (!match || !RIG_AUDIO_PATTERN.test(match.label || '')) {
                    try { localStorage.removeItem('directRxAudioId'); } catch (e) { /* ignore */ }
                    savedId = null;
                }
            }

            if (!rigDev && !savedId) {
                throw new Error('No rig audio input device found. Plug in the rig USB cable, grant audio permission, and reload.');
            }

            var deviceId = rigDev ? rigDev.deviceId : savedId;
            var c = Object.assign({ deviceId: { exact: deviceId } }, constraints);
            var stream = await navigator.mediaDevices.getUserMedia({ audio: c });
            try { localStorage.setItem('directRxAudioId', deviceId); } catch (e) { /* ignore */ }
            if (rigDev) {
                console.log('SerialRigTransport: using RX audio device:', rigDev.label);
            }
            return stream;
        }

        async _stopRxAudio() {
            if (!this._rxAudio.enabled && !this._rxAudio.ctx) return;
            this._rxAudio.enabled = false;
            this._emit('audioStatus', { enabled: false });
            if (this._rxAudio.node) {
                try { this._rxAudio.node.port.onmessage = null; } catch (e) { /* ignore */ }
                try { this._rxAudio.node.disconnect(); } catch (e) { /* ignore */ }
                this._rxAudio.node = null;
            }
            if (this._rxAudio.source) {
                try { this._rxAudio.source.disconnect(); } catch (e) { /* ignore */ }
                this._rxAudio.source = null;
            }
            if (this._rxAudio.stream) {
                try { this._rxAudio.stream.getTracks().forEach(function (t) { t.stop(); }); } catch (e) { /* ignore */ }
                this._rxAudio.stream = null;
            }
            if (this._rxAudio.ctx) {
                try { await this._rxAudio.ctx.close(); } catch (e) { /* ignore */ }
                this._rxAudio.ctx = null;
            }
        }

        // ----------------------------------------------------------------
        // Packet (WASM Direwolf) — RX feed + APRS UI-frame TX
        // ----------------------------------------------------------------

        async _packetEnsureModem() {
            if (this._packet.modem) return this._packet.modem;
            if (!this._packet.modulePromise) {
                this._packet.modulePromise = import('/wasm/direwolf-modem.js');
            }
            var mod = await this._packet.modulePromise;
            var modem = await mod.DirewolfModem.create();
            modem.init(this._packet.mode, 48000);
            // Cache the parser/builders so we don't re-look them up on
            // each RX frame.
            this._packet.parseAx25 = mod.parseAx25Frame;
            this._packet.aprsBuildPos = mod.aprsBuildUncompressedPosition;
            this._packet.aprsBuildLine = mod.aprsBuildMonitorLine;

            var self = this;
            modem.onFrame = function (chan, alevel, bytes) {
                var json = self._packet.parseAx25(bytes, chan, alevel);
                if (!json) return;
                json.type = 'packetRxFrame';
                self._emit('message', json);
            };
            this._packet.modem = modem;
            return modem;
        }

        _packetSetEnabled(on) {
            this._packet.enabled = !!on;
            if (on) {
                // Lazy-load on first enable so the WASM cost is opt-in.
                // Modem load is fine to do without a user gesture (it's
                // just a fetch + WASM compile), but the TX AudioContext
                // is NOT — we used to pre-init it here too, but packet.js
                // restores the saved PACKET-ON state from localStorage on
                // page load and that fires this handler outside any user
                // gesture, so the AudioContext creation rejected and the
                // cached rejected promise poisoned every later TX. Defer
                // audio init to the first TX click (which IS a gesture).
                this._packetEnsureModem().catch(function (err) {
                    console.error('packet: modem load failed:', err);
                });
            }
            this._emit('message', {
                type: 'packetStatus',
                enabled: this._packet.enabled,
                mode: this._packet.mode,
            });
        }

        _packetSetMode(baud) {
            if (baud !== 300 && baud !== 1200 && baud !== 9600) return;
            this._packet.mode = baud;
            if (this._packet.modem) this._packet.modem.init(baud, 48000);
            this._emit('message', {
                type: 'packetStatus',
                enabled: this._packet.enabled,
                mode: this._packet.mode,
            });
        }

        _packetSendChunk(audio, off, n, seq) {
            // Build a single 0x03 binary audio frame from a slice of the
            // encoded burst and ship it to sendAudioFrame() (where it gets
            // converted to Float32 and posted to the rig-USB-out worklet).
            var slice = audio.subarray(off, off + n);
            var buf = new ArrayBuffer(6 + slice.byteLength);
            var view = new DataView(buf);
            view.setUint8(0, 0x03);
            view.setUint8(1, 0x00);
            view.setUint16(2, seq & 0xFFFF, true);
            view.setUint16(4, 0, true);
            new Int16Array(buf, 6).set(slice);
            if (typeof this.sendAudioFrame === 'function') {
                this.sendAudioFrame(buf);
            }

            // Also tee the same audio back to the SPA as a 0x04 frame so
            // the packet panel can show the TX burst on its waterfall —
            // matches what the C++ wfweb webserver does (via TX-echo
            // 0x04 frames). rateDiv = sample rate / 1000.
            var echoBuf = new ArrayBuffer(6 + slice.byteLength);
            var echoView = new DataView(echoBuf);
            echoView.setUint8(0, 0x04);
            echoView.setUint8(1, 0x00);
            echoView.setUint16(2, seq & 0xFFFF, true);
            echoView.setUint16(4, 48, true);   // 48 samples/ms = 48 kHz
            new Int16Array(echoBuf, 6).set(slice);
            this.dispatchEvent(new CustomEvent('binary', { detail: echoBuf }));
        }

        async _packetTxBeacon(obj) {
            // SPA payload: { src, lat, lon, symTable, symCode, comment, path }.
            // Format → APRS uncompressed position → TNC monitor line → WASM
            // modem → audio. Audio is dispatched as 0x03 frames the same
            // way the SPA's mic capture worklet would, so the existing
            // sendAudioFrame() path forwards them to the rig USB out.
            var self = this;
            var fail = function (reason) {
                self._emit('message', { type: 'packetTxFailed', reason: reason });
            };
            // Re-entry guard: a second click while a TX is in flight would
            // race the PTT-on/-off enqueues against the in-flight queue and
            // leave the rig keyed (or the button disabled). Drop the second.
            if (this._packet.txInFlight) { fail('TX already in progress'); return; }
            if (!obj.src) { fail('missing source callsign'); return; }
            if (typeof obj.lat !== 'number' || typeof obj.lon !== 'number') {
                fail('missing or invalid lat/lon'); return;
            }
            this._packet.txInFlight = true;
            // Hard watchdog: independent of the try/finally below, force
            // an unkey + UI recovery after 30 s no matter what. Catches
            // the case where one of the awaits in the try block hangs
            // silently — finally never runs, so without this safety the
            // button stays disabled and the rig stays keyed forever.
            var watchdogFired = false;
            var watchdog = setTimeout(function () {
                if (self._packet.txInFlight) {
                    watchdogFired = true;
                    console.warn('packet TX: watchdog forcing unkey + recovery');
                    self.sendCommand({ cmd: 'setPTT', value: false });
                    self._emit('message', { type: 'packetTxFailed', reason: 'TX watchdog (30s)' });
                    self._packet.txInFlight = false;
                }
            }, 30000);
            try {
                var modem = await this._packetEnsureModem();
                var info = this._packet.aprsBuildPos(
                    obj.lat, obj.lon, obj.symTable, obj.symCode, obj.comment);
                var monitor = this._packet.aprsBuildLine(obj.src, info, obj.path);
                var audio = modem.txFrame(monitor);
                if (!audio) { fail('modem rejected frame'); return; }

                this._emit('message', { type: 'packetTxStarted' });
                // Surface the locally-originated frame in the monitor pane
                // so the operator sees what they just queued.
                this._emit('message', {
                    type: 'packetRxFrame', chan: 0, level: 0, ts: Date.now(),
                    src: obj.src, dst: 'APRS', path: obj.path || [],
                    info: info, ftype: 'UI', rawHex: '', _tx: true,
                });

                // Switch the rig to USB MOD INPUT so it actually modulates
                // the audio we're streaming over the USB cable. Same fix the
                // voice path applies on enableMic — without it the rig keys
                // but transmits empty carrier (the front-panel MIC source
                // sees nothing). Set both DATA-OFF (FM/USB voice) and DATA1
                // (USB-D / LSB-D data mode) so it works across all packet
                // bauds. We don't try to restore — the voice path's own
                // enableMic flow re-sets these for SSB voice.
                this._enqueue('setDataOffMod',
                    civ.cmdSetModInput(this._modIn.off, this._modIn.usbReg));
                this._enqueue('setDataMod',
                    civ.cmdSetModInput(this._modIn.data1, this._modIn.usbReg));

                // Make sure the TX audio context is initialised BEFORE we
                // key the rig and start pumping samples. This SHOULD already
                // be done from the pre-init in _packetSetEnabled, but cover
                // the case where the user clicked TX-Beacon before the
                // pre-init completed (within ~500 ms of PACKET ON).
                if (!this._txAudio.ctx) {
                    try {
                        await this._ensureTxAudioCtx();
                    } catch (e) {
                        console.warn('packet TX: audio ctx init failed:', e);
                    }
                }

                // Pre-buffer some audio chunks BEFORE keying PTT — same
                // pattern FT8's onFirstChunk uses (waits ~100 ms of audio
                // in the worklet ring buffer before firing setPTT). On a
                // cold start, posting to a freshly-created worklet and
                // immediately keying PTT can leave the rig keyed with the
                // first chunks not yet drained.
                var seq = 0;
                var CHUNK = 960;
                var PREBUFFER_CHUNKS = 5;  // ~100 ms @ 48 kHz
                var off = 0;
                for (var pre = 0; pre < PREBUFFER_CHUNKS && off < audio.length; pre++) {
                    var n = Math.min(CHUNK, audio.length - off);
                    this._packetSendChunk(audio, off, n, seq++);
                    off += n;
                }

                // Now key the rig. Route through sendCommand so
                // state.transmitting flips and the SPA's PTT indicator
                // lights up — same path FT8 / voice use.
                this.sendCommand({ cmd: 'setPTT', value: true });

                // Push the rest of the audio.
                while (off < audio.length) {
                    var rest = Math.min(CHUNK, audio.length - off);
                    this._packetSendChunk(audio, off, rest, seq++);
                    off += rest;
                }
                // Wait for the audio to play out, plus a tail so the
                // postamble flags fully unkey. await + try/finally below
                // guarantees PTT-off + packetTxComplete fire even if any
                // step earlier threw — the previous setTimeout-based path
                // could leave the button stuck if a second click clobbered
                // the in-flight enqueue.
                var durationMs = (audio.length / 48000) * 1000 + 150;
                await new Promise(function (r) { setTimeout(r, durationMs); });
            } catch (err) {
                console.error('packet TX:', err);
                fail(err && err.message ? err.message : String(err));
            } finally {
                clearTimeout(watchdog);
                // Skip cleanup if the watchdog already did it — we'd
                // otherwise emit packetTxComplete after packetTxFailed and
                // re-toggle the UI.
                if (!watchdogFired) {
                    this.sendCommand({ cmd: 'setPTT', value: false });
                    this._emit('message', { type: 'packetTxComplete' });
                    this._packet.txInFlight = false;
                }
            }
        }
    }

    SerialRigTransport.handles = function (cmd) { return !!HANDLED_CMDS[cmd]; };
    SerialRigTransport.rigDisplayName = rigDisplayName;

    global.SerialRigTransport = SerialRigTransport;
})(window);
