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

    var DEFAULT_MODES = [
        { num: 0, name: 'LSB' }, { num: 1, name: 'USB' },
        { num: 2, name: 'AM' },  { num: 3, name: 'CW' },
        { num: 4, name: 'RTTY' },{ num: 5, name: 'FM' },
        { num: 7, name: 'CW-R' },{ num: 8, name: 'RTTY-R' },
    ];
    var DEFAULT_FILTERS = [
        { num: 1, name: 'FIL1' }, { num: 2, name: 'FIL2' }, { num: 3, name: 'FIL3' },
    ];

    var HANDLED_CMDS = {
        getStatus: true, setFrequency: true, setMode: true,
        setFilter: true, setPTT: true,
        // claim audio cmds so they don't noisily fall back to a closed WS
        enableMic: true, enableAudio: true,
    };

    function lsGetInt(key) {
        try { var v = localStorage.getItem(key); return v ? parseInt(v) : null; }
        catch (e) { return null; }
    }
    function lsSetInt(key, val) {
        try { localStorage.setItem(key, String(val)); } catch (e) { /* ignore */ }
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
                    var on = !!obj.value;
                    this.state.transmitting = on;
                    this._enqueue('setPTT', civ.cmdSetPTT(on));
                    this._emit('update', { transmitting: on });
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

            // Initial reads
            this._enqueue('readFreq',   civ.cmdReadFrequency());
            this._enqueue('readMode',   civ.cmdReadMode());
            this._enqueue('readPTT',    civ.cmdReadPTT());
            this._enqueue('readSMeter', civ.cmdReadSMeter());
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

            var sMeterValue = civ.parseSMeterReply(payload);
            if (sMeterValue !== null) {
                if (sMeterValue !== this.state.sMeter) {
                    this.state.sMeter = sMeterValue;
                    this._emit('meters', { sMeter: sMeterValue });
                }
                return;
            }

            if (civ.parseAck(payload) !== null) return;
        }

        _emit(type, fields) {
            var msg = Object.assign({ type: type }, fields);
            this.dispatchEvent(new CustomEvent('message', { detail: msg }));
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
                audioAvailable: false,
                txAudioAvailable: false,
                hasPowerControl: false,
            });
        }

        _emitFullStatus() {
            this._emit('status', {
                frequency: this.state.frequency,
                vfoAFrequency: this.state.frequency,
                selectedVfo: 'A',
                mode: this.state.mode,
                filter: this.state.filter,
                sMeter: this.state.sMeter,
                transmitting: this.state.transmitting,
            });
        }

        _startPolling() {
            this._sMeterTimer = setInterval(() => {
                if (!this._open) return;
                if (this.state.transmitting) return;
                this._enqueue('readSMeter', civ.cmdReadSMeter());
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
