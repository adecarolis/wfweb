// Serial transport: drives the rig directly over Web Serial CI-V.
// No C++ server in the loop for control. Audio is intentionally not handled
// here — Phase 2 routes audio through Web Audio + setSinkId.
//
// Design:
//   * Maintains a small JS rig-state cache (frequency, mode, filter, PTT, sMeter).
//   * Synthesizes the same JSON message shapes the SPA already consumes
//     (rigInfo, status, update, meters) so handleMessage works unchanged.
//   * Outbound commands are funnelled through a deduping FIFO keyed by
//     command name, mirroring cachingQueue::addUnique() semantics.
//   * Inbound CI-V frames flow through IcomCiv.CivParser. Echoes (frames whose
//     "from" equals our controllerAddr) are filtered out by the parser.
//   * Polls the S-meter while RX. Frequency / mode arrive via Icom's transceive
//     pushes when enabled on the rig (default for most models); a 1s safety
//     poll catches rigs that have transceive disabled.

(function (global) {
    'use strict';

    if (!global.RigTransport) {
        console.error('serial-transport.js: RigTransport base class not loaded');
        return;
    }
    if (!global.IcomCiv) {
        console.error('serial-transport.js: IcomCiv codec not loaded');
        return;
    }

    var civ = global.IcomCiv;

    // CI-V address → display name. Known Icom rigs only; extend as needed.
    var KNOWN_RIGS = {
        0x94: 'IC-7300',
        0x98: 'IC-7610',
        0xA2: 'IC-9700',
        0xA4: 'IC-705',
        0x8C: 'IC-7100',
        0x70: 'IC-9100',
    };

    var DEFAULT_MODES = [
        { num: 0, name: 'LSB' },
        { num: 1, name: 'USB' },
        { num: 2, name: 'AM' },
        { num: 3, name: 'CW' },
        { num: 4, name: 'RTTY' },
        { num: 5, name: 'FM' },
        { num: 7, name: 'CW-R' },
        { num: 8, name: 'RTTY-R' },
    ];

    var DEFAULT_FILTERS = [
        { num: 1, name: 'FIL1' },
        { num: 2, name: 'FIL2' },
        { num: 3, name: 'FIL3' },
    ];

    // Commands SerialRigTransport currently knows how to handle.
    // Anything else falls through to the WebSocket transport.
    var HANDLED_CMDS = {
        getStatus: true,
        setFrequency: true,
        setMode: true,
        setFilter: true,
        setPTT: true,
        // enableMic / enableAudio: in Phase 1 we don't have an audio path
        // either way (no server reachable, no setSinkId yet), so claim them
        // here to silently drop instead of letting them noisily fail on the
        // WS side.
        enableMic: true,
        enableAudio: true,
    };

    class SerialRigTransport extends global.RigTransport {
        constructor(opts) {
            super();
            opts = opts || {};
            this.civAddr = opts.civAddr || 0x94;       // default: IC-7300
            this.controllerAddr = opts.controllerAddr || 0xE0;
            this.baudRate = opts.baudRate || 19200;
            this.parser = new civ.CivParser({
                controllerAddr: this.controllerAddr,
                onFrame: this._onFrame.bind(this),
            });

            this.port = null;
            this.writer = null;
            this.reader = null;
            this._open = false;

            // FIFO of pending command keys; deduped by key.
            this._queue = [];
            this._payloads = {};
            this._draining = false;

            // Polling timers.
            this._sMeterTimer = null;
            this._safetyPollTimer = null;
            this._lastFreqStamp = 0;
            this._lastModeStamp = 0;

            // Rig state cache. Mirrors what the SPA reads out of status/update.
            this.state = {
                frequency: 0,
                mode: null,
                filter: null,
                sMeter: 0,
                transmitting: false,
            };
        }

        // ----------------------------------------------------------------
        // RigTransport interface
        // ----------------------------------------------------------------

        async connect(prePicked) {
            if (this._open) return;
            var port = prePicked || (await navigator.serial.requestPort({}));
            await port.open({
                baudRate: this.baudRate,
                dataBits: 8,
                stopBits: 1,
                parity: 'none',
                flowControl: 'none',
            });
            this.port = port;
            this.writer = port.writable.getWriter();
            this._open = true;

            // Kick off the read loop. Errors propagate as a 'close' event.
            this._readLoop().catch((err) => {
                console.error('SerialRigTransport read loop ended:', err);
                this._notifyClose(err);
            });

            this.dispatchEvent(new Event('open'));

            // Push synthesized rigInfo + initial polls so the UI populates
            // before any rig replies.
            this._emitRigInfo();

            // Initial reads: freq, mode, PTT, S-meter.
            this._enqueue('readFreq', civ.cmdReadFrequency());
            this._enqueue('readMode', civ.cmdReadMode());
            this._enqueue('readPTT',  civ.cmdReadPTT());
            this._enqueue('readSMeter', civ.cmdReadSMeter());

            this._startPolling();
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
                    this._emit('update', {
                        frequency: obj.value,
                        vfoAFrequency: obj.value,
                    });
                    return;
                case 'setMode':
                    if (typeof obj.value !== 'string') return;
                    this.state.mode = obj.value;
                    var fil = this.state.filter || 1;
                    this._enqueue('setMode', civ.cmdSetMode(obj.value, fil));
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
                // Audio plumbing — Phase 2 owns this; in Phase 1 we no-op.
                case 'enableMic':
                case 'enableAudio':
                    return;
                // Anything else is silently ignored for now. Will fill in
                // (gains, NB/NR/ANF, tuner, preamp, attenuator, …) as we go.
                default:
                    return;
            }
        }

        sendAudioFrame(/* buffer */) {
            // Phase 2 wires this to setSinkId; for now, drop.
        }

        // ----------------------------------------------------------------
        // Internals
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
                    // Slight inter-frame gap so the rig has time to clear
                    // the bus before the next command. 5 ms is enough at 19200.
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
                // Tiny delay before reattaching to avoid a tight error loop.
                await new Promise(function (r) { setTimeout(r, 50); });
            }
        }

        _onFrame(frame) {
            // 'to' may be the controller (0xE0) for solicited replies, or 0x00
            // for transceive broadcasts. We accept both.
            var payload = frame.payload;
            if (!payload || payload.length === 0) return;

            // Frequency
            var freqHz = civ.parseFrequencyReply(payload);
            if (freqHz !== null && freqHz > 0) {
                this._lastFreqStamp = Date.now();
                if (freqHz !== this.state.frequency) {
                    this.state.frequency = freqHz;
                    this._emit('update', {
                        frequency: freqHz,
                        vfoAFrequency: freqHz,
                    });
                }
                return;
            }

            // Mode + filter
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

            // PTT
            var pttState = civ.parsePTTReply(payload);
            if (pttState !== null) {
                if (pttState !== this.state.transmitting) {
                    this.state.transmitting = pttState;
                    this._emit('update', { transmitting: pttState });
                }
                return;
            }

            // S-meter
            var sMeterValue = civ.parseSMeterReply(payload);
            if (sMeterValue !== null) {
                if (sMeterValue !== this.state.sMeter) {
                    this.state.sMeter = sMeterValue;
                    this._emit('meters', { sMeter: sMeterValue });
                }
                return;
            }

            // Acks (FB/FA) — useful for confirming sets; ignore for now.
            if (civ.parseAck(payload) !== null) return;
        }

        _emit(type, fields) {
            var msg = Object.assign({ type: type }, fields);
            this.dispatchEvent(new CustomEvent('message', { detail: msg }));
        }

        _emitRigInfo() {
            this._emit('rigInfo', {
                version: '0.6.1-direct',
                model: KNOWN_RIGS[this.civAddr] || ('Icom 0x' + this.civAddr.toString(16)),
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
            // S-meter every 200ms while RX.
            this._sMeterTimer = setInterval(() => {
                if (!this._open) return;
                if (this.state.transmitting) return;
                this._enqueue('readSMeter', civ.cmdReadSMeter());
            }, 200);
            // Safety poll: if we haven't seen freq/mode pushes in 2s, ask.
            // Catches rigs with transceive disabled.
            this._safetyPollTimer = setInterval(() => {
                if (!this._open) return;
                var now = Date.now();
                if (now - this._lastFreqStamp > 2000) {
                    this._enqueue('readFreq', civ.cmdReadFrequency());
                }
                if (now - this._lastModeStamp > 2000) {
                    this._enqueue('readMode', civ.cmdReadMode());
                }
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

    global.SerialRigTransport = SerialRigTransport;
})(window);
