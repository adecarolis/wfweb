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

    // CI-V address → display name. Extend as needed.
    var KNOWN_RIGS = {
        0x70: 'IC-9100',
        0x88: 'IC-7000',
        0x8C: 'IC-7100',
        0x94: 'IC-7300',
        0x98: 'IC-7610',
        0xA2: 'IC-9700',
        0xA4: 'IC-705',
        0xAC: 'IC-905',
    };

    // Bauds tried during auto-detect, ordered by likelihood. 19200 is the
    // most common Icom default; 115200 is what people set for snappy SDR
    // operation; 9600 is the original factory default for older rigs.
    var PROBE_BAUDS = [19200, 115200, 9600, 57600, 38400, 4800];
    var PROBE_TIMEOUT_MS = 500;

    // The SPA's populateModes() expects a flat array of mode-name strings —
    // mirrors what the C++ webserver sends.
    var DEFAULT_MODES = ['LSB', 'USB', 'AM', 'CW', 'RTTY', 'FM', 'CW-R', 'RTTY-R'];
    // populateFilters() takes [{num, name}, ...]
    var DEFAULT_FILTERS = [
        { num: 1, name: 'FIL1' }, { num: 2, name: 'FIL2' }, { num: 3, name: 'FIL3' },
    ];

    // Icom S-meter raw BCD (0..241) → dB relative to S9 (-54..+60), the
    // scale the SPA's drawSMeter() expects.
    //   0   → -54 dB (S0)
    //   120 →   0 dB (S9)
    //   241 → +60 dB (S9+60)
    function rawSMeterToDb(raw) {
        if (raw <= 120) return (raw - 120) * 54 / 120;   // -54..0
        return Math.min(60, (raw - 120) * 60 / 121);     // 0..60
    }

    // SPA command name → 0x14 NN sub byte (analog levels 0..255)
    var ANALOG_SUB = {
        setAfGain:      0x01,
        setRfGain:      0x02,
        setSquelch:     0x03,
        setRfPower:     0x0A,
        setMicGain:     0x0B,
        setMonitorGain: 0x0F,
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
        0x0F: 'monitorGain',
        0x07: 'pbtInner',
        0x08: 'pbtOuter',
        0x0C: 'cwSpeed',
    };

    // SPA command name → 0x16 NN sub byte (bool toggles)
    var BOOL_SUB = {
        setAutoNotch:      0x41,
        setNoiseBlanker:   0x22,
        setNoiseReduction: 0x40,
    };
    var BOOL_SUB_TO_FIELD = {
        0x41: 'autoNotch',
        0x22: 'nb',
        0x40: 'nr',
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
        setPreamp: true, setAttenuator: true,
        // VFO ops
        selectVFO: true, swapVFO: true, equalizeVFO: true, setSplit: true,
        // Misc
        setTuner: true, setPower: true,
        // CW
        sendCW: true, stopCW: true,
        // Anything else (FreeDV, packet, memory, span, filter shape, LAN
        // ops, reporters …) falls through to the WS path which is closed in
        // Direct mode.
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

            // Spectrum scope assembly state. The Icom scope command (0x27 0x00)
            // arrives as a sequence of frames: seq 1 carries header (mode +
            // start/end freq + out-of-range flag), seq 2..N carry pixels.
            this._scope = {
                seqMax: 0, mode: 0,
                startFreq: 0, endFreq: 0,
                pixels: [], capturing: false,
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
                    this._enqueue('setMode', civ.cmdSetMode(obj.value, this.state.filter || 1));
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
                case 'setPTT':
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
                case 'sendCW':
                    if (typeof obj.text !== 'string' || !obj.text.length) return;
                    this._enqueue('sendCW', civ.cmdSendCW(obj.text));
                    return;
                case 'stopCW':
                    this._enqueue('stopCW', civ.cmdStopCW());
                    return;
                case 'enableMic':
                case 'enableAudio':
                    return;
                default:
                    return;
            }
        }

        sendAudioFrame(/* buffer */) { /* Phase 2 */ }

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
            lsSetInt('directBaudRate', baud);
            lsSetInt('directCivAddr', addr);

            this.port = port;
            this.writer = port.writable.getWriter();
            this._open = true;

            this.onProgress({
                stage: 'connected', baud: baud, civAddr: addr,
                model: KNOWN_RIGS[addr] || ('Icom 0x' + addr.toString(16).toUpperCase()),
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
                var sMeterDb = rawSMeterToDb(sMeterRaw);
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
                    var v = civ.parseTxMeterReply(payload, sub);
                    if (v !== null && this.state[meterField] !== v) {
                        this.state[meterField] = v;
                        var m = {}; m[meterField] = v;
                        this._emit('meters', m);
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

        _emitRigInfo() {
            this._emit('rigInfo', {
                version: '0.6.1-direct',
                model: KNOWN_RIGS[this.civAddr] || ('Icom 0x' + this.civAddr.toString(16).toUpperCase()),
                connected: true,
                modes: DEFAULT_MODES,
                filters: DEFAULT_FILTERS,
                hasFilterSettings: true,
                hasMainSub: false,
                hasSpectrum: true,    // we'll stream scope data via 0x27 0x00
                spectAmpMax: 160,     // Icom amplitude scale (matches C++ wfweb)
                audioAvailable: false,
                txAudioAvailable: false,
                hasPowerControl: false,
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
                'split', 'tuner'];
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
            this.dispatchEvent(new CustomEvent('close', { detail: { reason: reason } }));
        }
    }

    SerialRigTransport.handles = function (cmd) { return !!HANDLED_CMDS[cmd]; };
    SerialRigTransport.knownRigs = KNOWN_RIGS;

    global.SerialRigTransport = SerialRigTransport;
})(window);
