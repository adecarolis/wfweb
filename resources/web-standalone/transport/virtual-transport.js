// Virtual rig transport. No real hardware — the rig's bus is a same-origin
// BroadcastChannel (AirBus). Every browser tab on /?virtual=1 spins up one
// of these and they all hear each other's TX as RX.
//
// Phase 1: in-memory CI-V state, raw audio across the bus, no modems.
// Phase 2: synthetic S-meter from incoming bus peak.
// Phase 3: RADE V1 modem wired in — TX-intercept on sendAudioFrame, RX-tee
//          on _onAirRx. Direwolf/packet hooks land here next.

(function (global) {
    'use strict';

    if (!global.RigTransport || !global.AirBus) {
        console.error('virtual-transport.js: missing prerequisite modules');
        return;
    }

    var DEFAULT_MODES   = ['LSB', 'USB', 'AM', 'CW', 'RTTY', 'FM', 'CW-R', 'RTTY-R'];
    var DEFAULT_FILTERS = [
        { num: 1, name: 'FIL1' }, { num: 2, name: 'FIL2' }, { num: 3, name: 'FIL3' },
    ];
    var DEFAULT_SPANS = [
        { num: 0, name: '±2.5k', hz: 2500   }, { num: 1, name: '±5k',   hz: 5000   },
        { num: 2, name: '±10k',  hz: 10000  }, { num: 3, name: '±25k',  hz: 25000  },
        { num: 4, name: '±50k',  hz: 50000  }, { num: 5, name: '±100k', hz: 100000 },
        { num: 6, name: '±250k', hz: 250000 },
    ];

    // Commands that simply echo their value back as an 'update' field of the
    // same name. Keeps the table short — exotic commands fall through to a
    // no-op (the SPA never blocks on a missing reply for these).
    var ECHO_FIELDS = {
        setAfGain:        'afGain',
        setRfGain:        'rfGain',
        setRfPower:       'rfPower',
        setSquelch:       'squelch',
        setMicGain:       'micGain',
        setMonitorGain:   'monitorGain',
        setPBTInner:      'pbtInner',
        setPBTOuter:      'pbtOuter',
        setCWSpeed:       'cwSpeed',
        setAutoNotch:     'autoNotch',
        setNoiseBlanker:  'nb',
        setNoiseReduction:'nr',
        setMonitor:       'monitor',
        setPreamp:        'preamp',
        setAttenuator:    'attenuator',
        setFilter:        'filter',
        setFilterWidth:   'filterWidth',
        setSplit:         'split',
        setTuner:         'tuner',
        setSpan:          'spanIndex',
    };

    // Convert a peak Int16 amplitude to the SPA's S-meter scale: dB relative
    // to S9 (0 = S9, -54 = S0). drawSMeter() expects this units.
    function peakToDb(peak) {
        if (peak <= 0) return -54;
        var db = 20 * Math.log10(peak / 32767);
        if (db < -54) return -54;
        if (db > 60)  return 60;
        return db;
    }

    class VirtualRigTransport extends global.RigTransport {
        constructor(opts) {
            super();
            opts = opts || {};
            this.rigId = (opts.rigId | 0) || 1;
            this._open = false;
            this._bus = null;
            this._rxSeq = 0;

            this.state = {
                frequency: 14074000,
                mode: 'USB',
                filter: 1,
                transmitting: false,
                // -54 dB = S0. The drawSMeter() scale treats 0 as S9, so a
                // default of 0 would paint a permanent full-scale signal.
                sMeter: -54,
            };
            this._audioEnabled = false;

            // S-meter peak detector. _onAirRx tracks max |sample| in the
            // current 200 ms window; a timer drains the peak, emits a meters
            // message, and resets. No audio in the window → S0 baseline.
            // Same shape for the TX-side power/ALC peak.
            this._sMeterPeak = 0;
            this._txMeterPeak = 0;
            this._sMeterTimer = null;

            // RADE V1 voice modem state. Lazy-loaded — the WASM payload is
            // ~10 MB and we only pull it down when the user picks RADE mode.
            // While `txActive` is on, mic samples arriving via sendAudioFrame
            // are diverted into modem.processTx instead of going raw to the
            // bus; the modem's 'txReady' listener pushes the encoded modem
            // audio to the bus instead.
            this._rade = {
                enabled: false, modem: null, modulePromise: null,
                txActive: false, eooSent: false,
                rxSpeechSeq: 0, callsign: null,
            };
        }

        async connect() {
            if (this._open) return;
            this._bus = new global.AirBus(this.rigId);
            var self = this;
            this._bus.addEventListener('rx', function (e) { self._onAirRx(e.detail); });
            this._open = true;
            this._sMeterTimer = setInterval(function () { self._tickSMeter(); }, 200);
            this.dispatchEvent(new Event('open'));
            this._emitRigInfo();
        }

        async tryAutoReconnect() { await this.connect(); return true; }

        async close() {
            if (!this._open) return;
            this._open = false;
            if (this._sMeterTimer) { clearInterval(this._sMeterTimer); this._sMeterTimer = null; }
            if (this._bus) { this._bus.close(); this._bus = null; }
            this.dispatchEvent(new CustomEvent('close', { detail: { reason: 'user-close' } }));
        }

        isOpen() { return this._open; }

        sendCommand(obj) {
            if (!this._open || !obj || !obj.cmd) return;

            if (ECHO_FIELDS.hasOwnProperty(obj.cmd)) {
                var field = ECHO_FIELDS[obj.cmd];
                var val = (obj.cmd === 'setCWSpeed') ? obj.wpm : obj.value;
                if (val === undefined) return;
                this.state[field] = val;
                var upd = {}; upd[field] = val;
                this._emit('update', upd);
                return;
            }

            switch (obj.cmd) {
                case 'getStatus':
                    this._emitFullStatus();
                    return;
                case 'setFrequency':
                    if (typeof obj.value !== 'number' || obj.value <= 0) return;
                    this.state.frequency = obj.value;
                    this._emit('update', { frequency: obj.value, vfoAFrequency: obj.value });
                    return;
                case 'setMode':
                    if (typeof obj.value !== 'string') return;
                    this.state.mode = obj.value;
                    this._emit('update', { mode: obj.value });
                    return;
                case 'setPTT':
                    var ptt = !!obj.value;
                    if (this._rade.enabled && this._rade.modem) {
                        // Mirror SerialRigTransport: on PTT-off ship the EOO
                        // (carries the encoded callsign — receiver decodes it
                        // after sync drops) before clearing transmitting.
                        if (ptt) {
                            this._rade.txActive = true;
                            this._rade.eooSent  = false;
                            this.state.transmitting = true;
                            this._emit('update', { transmitting: true });
                        } else {
                            this._radePttOff();
                        }
                        return;
                    }
                    this.state.transmitting = ptt;
                    this._emit('update', { transmitting: ptt });
                    return;
                case 'selectVFO':
                    this._emit('update', { selectedVfo: obj.value || 'A' });
                    return;
                case 'enableAudio':
                    // Mirror SerialRigTransport: the SPA's startAudio() flips
                    // audioEnabled to true only after the transport echoes
                    // audioStatus back. Without this, handleAudioData drops
                    // every incoming 0x02 frame and PTT redirects to the
                    // "enable audio" menu instead of transmitting.
                    this._audioEnabled = !!obj.value;
                    this._emit('audioStatus', {
                        enabled: this._audioEnabled,
                        sampleRate: 48000,
                    });
                    return;
                case 'enableMic':
                    // Mic capture lives entirely on the SPA side (it grabs
                    // the local laptop mic via getUserMedia and ships 0x03
                    // frames to sendAudioFrame). Nothing to do here.
                    return;
                case 'setRadeMode':
                    if (obj.value) {
                        var self = this;
                        this._radeEnable().catch(function (err) {
                            console.error('VirtualRigTransport: rade enable failed:', err);
                            self._emit('message', { type: 'radeStatus', enabled: false, error: String(err) });
                        });
                    } else {
                        this._radeDisable();
                    }
                    return;
                case 'setRadeCallsign':
                    this._rade.callsign = String(obj.value || '').toUpperCase().trim();
                    if (this._rade.modem && this._rade.callsign) {
                        try { this._rade.modem.setTxCallsign(this._rade.callsign); }
                        catch (e) { console.warn('rade setTxCallsign:', e); }
                    }
                    return;
                // Phase-3 stubs. Accepting them keeps the UI controls live;
                // the modems hook in here later.
                case 'packetEnable': case 'packetSetMode':
                case 'aprsTxBeacon': case 'aprsBeaconConfig': case 'aprsClearStations':
                case 'termList': case 'termRegister': case 'termConnect':
                case 'termDisconnect': case 'termSend': case 'termHistory':
                case 'sendCW': case 'stopCW':
                case 'swapVFO': case 'equalizeVFO':
                case 'setPower':
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
            if (!this._bus) return;

            var src = new Int16Array(buffer, 6);

            // Track TX peak for the Po/ALC meters. _tickSMeter drains it.
            for (var pi = 0; pi < src.length; pi++) {
                var pv = src[pi];
                if (pv < 0) pv = -pv;
                if (pv > this._txMeterPeak) this._txMeterPeak = pv;
            }

            // RADE TX intercept: divert mic samples into the modem encoder.
            // The modem will fire txReady with encoded audio that we ship to
            // the bus — never push raw mic onto the bus while RADE TX is
            // active, otherwise the receiver sees voice + modem layered.
            if (this._rade.enabled && this._rade.txActive && this._rade.modem) {
                try { this._rade.modem.processTx(src); }
                catch (e) { console.warn('rade processTx:', e); }
                return;
            }

            // Copy out — the SPA reuses its source buffer across frames, so
            // we can't transfer it. The AirBus then transfers our copy.
            var copy = new Int16Array(src.length);
            copy.set(src);
            this._bus.tx(copy);
        }

        // ----------------------------------------------------------------
        // Internals
        // ----------------------------------------------------------------

        _onAirRx(msg) {
            var samples = msg.samples;
            if (!samples || samples.length === 0) return;

            // Track peak amplitude for the synthetic S-meter; _tickSMeter
            // drains it every 200 ms.
            for (var i = 0; i < samples.length; i++) {
                var v = samples[i];
                if (v < 0) v = -v;
                if (v > this._sMeterPeak) this._sMeterPeak = v;
            }

            // RADE RX tee: hand the same 48 kHz Int16 samples to the modem.
            // While RADE is on, suppress raw 0x02 emit — speakers play the
            // synthesized speech that comes back via 'rxSpeech' instead.
            if (this._rade.enabled && this._rade.modem) {
                try { this._rade.modem.processRx(samples); }
                catch (e) { console.warn('rade processRx:', e); }
                return;
            }

            // Re-emit as a 0x02 frame so the SPA's existing audio path picks
            // it up (handleBinaryMessage → handleAudioData → AudioWorklet).
            var buf = new ArrayBuffer(6 + samples.byteLength);
            var view = new DataView(buf);
            view.setUint8(0, 0x02);
            view.setUint8(1, 0x00);
            view.setUint16(2, (this._rxSeq++) & 0xFFFF, true);
            view.setUint16(4, 0, true);
            new Int16Array(buf, 6).set(samples);
            this.dispatchEvent(new CustomEvent('binary', { detail: buf }));
        }

        _tickSMeter() {
            var rxPeak = this._sMeterPeak;
            var txPeak = this._txMeterPeak;
            this._sMeterPeak = 0;
            this._txMeterPeak = 0;

            if (this.state.transmitting) {
                // Map full-scale Int16 → 100 W (drawTxMeters' Po scale tops
                // at 100 W). ALC follows the same peak as a 0..1 fraction.
                // SWR sits at 1.0 — there's no real load, but a 0/1.0 swing
                // makes the meter look perma-broken.
                var frac = txPeak / 32767;
                if (frac > 1) frac = 1;
                this._emit('meters', {
                    powerMeter: frac * 100,
                    swrMeter: 1.0,
                    alcMeter: frac,
                });
                return;
            }

            var dB = peakToDb(rxPeak);
            if (dB === this.state.sMeter) return;
            this.state.sMeter = dB;
            this._emit('meters', { sMeter: dB });
        }

        _emit(type, fields) {
            var msg = Object.assign({ type: type }, fields);
            this.dispatchEvent(new CustomEvent('message', { detail: msg }));
        }

        _emitRigInfo() {
            this._emit('rigInfo', {
                version: '0.6.1-virtual',
                model: 'Virtual Rig #' + this.rigId,
                connected: true,
                modes: DEFAULT_MODES,
                filters: DEFAULT_FILTERS,
                spans: DEFAULT_SPANS,
                hasFilterSettings: true,
                hasMainSub: false,
                hasSpectrum: false,
                spectAmpMax: 160,
                audioAvailable: true,
                audioSampleRate: 48000,
                txAudioAvailable: true,
                hasPowerControl: false,
                hasTransmit: true,
                hasLAN: false,
                numReceivers: 1,
                numVFOs: 2,
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
            this._emit('status', s);
        }

        // ----------------------------------------------------------------
        // RADE V1 (mirrors SerialRigTransport's _radeEnsureModem path, but
        // routes TX out to the AirBus instead of the rig's USB output, and
        // pulls RX off the bus instead of getUserMedia).
        // ----------------------------------------------------------------

        async _radeEnsureModem() {
            if (this._rade.modem) return this._rade.modem;
            if (!this._rade.modulePromise) {
                this._rade.modulePromise = import('/wasm/rade-modem.js');
            }
            var mod = await this._rade.modulePromise;
            var modem = await mod.RadeModem.create();
            modem.init(48000);
            if (this._rade.callsign) modem.setTxCallsign(this._rade.callsign);

            var self = this;

            // Decoded speech samples → re-emit as 0x02 frames so the SPA
            // plays them through the existing speaker pipeline.
            modem.addEventListener('rxSpeech', function (ev) {
                var samples = ev.detail.samples;
                if (!samples || !samples.length) return;
                var buf = new ArrayBuffer(6 + samples.byteLength);
                var view = new DataView(buf);
                view.setUint8(0, 0x02);
                view.setUint8(1, 0x00);
                view.setUint16(2, (self._rade.rxSpeechSeq++) & 0xFFFF, true);
                view.setUint16(4, 0, true);
                new Int16Array(buf, 6).set(samples);
                self.dispatchEvent(new CustomEvent('binary', { detail: buf }));
            });

            // Encoded modem audio → ship to the bus. This is the only TX path
            // for RADE in virtual mode; sendAudioFrame's RADE intercept makes
            // sure raw mic doesn't also leak onto the bus.
            modem.addEventListener('txReady', function (ev) {
                var samples = ev.detail.samples;
                if (!samples || !samples.length || !self._bus) return;
                var copy = new Int16Array(samples.length);
                copy.set(samples);
                self._bus.tx(copy);
            });

            modem.addEventListener('statsUpdate', function (ev) {
                self._emit('message', {
                    type: 'radeStats',
                    snr: ev.detail.snr,
                    sync: ev.detail.sync,
                    freqOffset: ev.detail.freqOffset,
                });
            });
            modem.addEventListener('rxCallsign', function (ev) {
                self._emit('message', { type: 'radeCallsign', callsign: ev.detail.callsign });
            });

            this._rade.modem = modem;
            return modem;
        }

        async _radeEnable() {
            await this._radeEnsureModem();
            this._rade.enabled = true;
            this._emit('message', { type: 'radeStatus', enabled: true });
        }

        _radeDisable() {
            if (!this._rade.enabled) return;
            this._rade.enabled = false;
            this._rade.txActive = false;
            this._rade.eooSent  = false;
            this._emit('message', { type: 'radeStatus', enabled: false });
        }

        // PTT-off path while RADE is live: ship the EOO frame (carries the
        // encoded callsign — receiver decodes after sync drops), wait long
        // enough for the buffered audio to play out, then clear transmitting.
        _radePttOff() {
            this._rade.txActive = false;
            try {
                if (!this._rade.eooSent && this._rade.modem) {
                    var eoo = this._rade.modem.generateEooAudio();
                    this._rade.eooSent = true;
                    if (eoo && eoo.length && this._bus) {
                        var copy = new Int16Array(eoo.length);
                        copy.set(eoo);
                        this._bus.tx(copy);
                    }
                }
            } catch (e) { console.warn('rade EOO:', e); }
            // Match RadeProcessor's 300 ms unkey delay so the EOO clears the
            // bus before the receiver loses sync entirely.
            var self = this;
            setTimeout(function () {
                self.state.transmitting = false;
                self._emit('update', { transmitting: false });
            }, 300);
        }
    }

    global.VirtualRigTransport = VirtualRigTransport;
})(window);
