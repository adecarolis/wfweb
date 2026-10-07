/*
 * rx-jitter.js — adaptive jitter buffer for the RX audio stream (both builds).
 *
 * RX PCM reaches the page in frames (WebSocket on the server build, the
 * capture worklet on standalone) whose arrival times jitter: the network
 * stalls and then delivers a burst, the main thread is busy painting a
 * waterfall, the rig's audio clock and the sound card's clock drift apart.
 * The buffer between those frames and the speaker trades latency for
 * immunity to that jitter. This one measures the jitter it actually sees and
 * keeps the buffer as short as that allows:
 *
 *   - An underrun is caught `reserve` early, so the tail fades out instead of
 *     clicking, and the stream re-buffers to the target before it fades back
 *     in. The gap that caused it (silence between two frames, in played
 *     samples) is measured; the target grows to cover a repeat of it.
 *   - Every 2 s the buffer records the stall budget that period needed. The
 *     target shrinks back to the largest budget seen in the last `hold`
 *     periods, so a one-off stall costs latency for a minute, a periodic one
 *     keeps the buffer it needs. Another underrun within twice the hold
 *     doubles it (up to 10 min), so repeating stalls stop causing bumps.
 *   - The floor (the lowest depth in a period, i.e. the trough of the
 *     frame-by-frame sawtooth) is steered onto the target: a small error is
 *     resampled away at up to +2 % / -0.4 % — inaudible, and it also absorbs
 *     clock drift between the rig and the sound card — and a large excess is
 *     skipped with a crossfade, at a quiet moment when one comes within 1.5 s.
 *     A large excess already present when playback starts (frames queued
 *     while the audio engine was not pulling) is skipped at once: it is stale.
 *
 * `push(samples)` on the producer side, `pull(out)` on the consumer side
 * (any block size: the worklet's 128, the ScriptProcessor's 4096). The class
 * has no DOM or Web Audio dependency, so it runs on the main thread, inside an
 * AudioWorkletGlobalScope (wfRxWorkletUrl() ships its source there) and under
 * node for tests/rx_jitter_sim.mjs.
 */
'use strict';

class WfJitterBuffer {
    // opts: { targetMs: floor target to start from (restored from the last
    //         session; held for one hold time), lockMs: pin the buffer to this
    //         many ms and stop adapting }
    constructor(sampleRate, opts) {
        opts = opts || {};
        var ms = this.ms = sampleRate / 1000;
        this.ring = new Float32Array(Math.round(sampleRate * 3));
        this.wr = 0; this.rd = 0; this.frac = 0; this.depth = 0;
        this.started = false;
        this.rate = 1;
        this.fade = Math.round(5 * ms);        // fade-in, fade-out and crossfade length
        this.reserve = this.fade;              // underrun is declared this early so the tail can fade
        this.safety = Math.round(10 * ms);     // added on top of a measured gap
        this.skipExcess = Math.round(250 * ms); // excess above the target that is skipped, not resampled
        this.minTarget = Math.round(20 * ms);  // floor target bounds
        this.maxTarget = Math.round(1000 * ms);
        this.target = this.minTarget;
        this.lock = 0;                         // pinned total buffer in samples, 0 = adaptive
        this.avgPush = 0;                      // typical frame size (EMA)
        this.block = 0;                        // consumer block size (gap resolution)
        this.clock = 0;                        // samples rendered, silence included
        this.lastPush = -1;                    // clock at the previous push, -1 = none
        this.period = Math.round(2000 * ms); this.periodPos = 0;
        this.periodMin = Infinity;             // lowest depth seen this period (the floor)
        this.periodGap = 0;                    // longest push-to-push gap this period
        this.hist = new Float64Array(300);     // per-period stall budget, 10 min
        this.histPos = 0; this.histLen = 0;
        this.holdMin = 30; this.holdMax = 300; // periods: 60 s .. 10 min
        this.hold = this.holdMin;
        this.lastUnderrun = -1; this.underrunOpen = false;
        this.fadeIn = 0; this.xfadeFrom = 0; this.xfadeLeft = 0;
        this.pendingSkip = 0; this.skipDeadline = 0;
        this.energy = 0; this.env = 0;         // last block energy, slow average
        this.underruns = 0; this.skips = 0; this.overflows = 0;
        this.onEvent = null;
        if (opts.lockMs > 0) this.setLock(opts.lockMs);
        else if (opts.targetMs > 0) {
            this.target = this._clampTarget(Math.round(opts.targetMs * ms));
            for (var i = 0; i < this.holdMin; i++) this.hist[i] = this.target;
            this.histPos = this.holdMin; this.histLen = this.holdMin;
        }
    }

    _clampTarget(t) { return Math.max(this.minTarget, Math.min(this.maxTarget, t)); }

    // Pin the total buffer (floor + one frame) to `ms`; 0 or null resumes adapting.
    setLock(ms) {
        this.lock = ms > 0 ? Math.round(ms * this.ms) : 0;
        if (this.lock) this._applyLock();
    }
    _applyLock() { this.target = this._clampTarget(this.lock - Math.round(this.avgPush)); }

    push(samples) {
        var n = samples.length, ring = this.ring, len = ring.length;
        if (!n) return;
        if (n > len) { samples = samples.subarray(n - len); n = len; }
        if (this.lastPush >= 0) {
            var gap = this.clock - this.lastPush;
            // Longer than any buffer: the stream was stopped (TX, mode change), not jitter.
            if (gap <= this.maxTarget * 2) {
                if (gap > this.periodGap) this.periodGap = gap;
                if (this.underrunOpen) this._grow(gap);
            }
        }
        this.underrunOpen = false;
        this.lastPush = this.clock;
        this.avgPush = this.avgPush ? this.avgPush + (n - this.avgPush) * 0.05 : n;
        var free = len - this.depth;
        if (n > free) {                        // the writer would lap the reader: drop the oldest
            var drop = n - free;
            this.rd = (this.rd + drop) % len; this.depth -= drop; this.overflows++;
            this.xfadeLeft = 0; this.pendingSkip = 0;
        }
        var wr = this.wr, first = Math.min(n, len - wr);
        ring.set(first === n ? samples : samples.subarray(0, first), wr);
        if (first < n) ring.set(samples.subarray(first), 0);
        this.wr = (wr + n) % len; this.depth += n;
    }

    pull(out) {
        var n = this.block = out.length;
        var minT = n + this.reserve + 2 + this.safety;   // a block, plus margin, must fit under the floor
        if (minT > this.minTarget) { this.minTarget = minT; if (this.target < minT) this.target = minT; }
        if (!this.started) {
            if (this.avgPush && this.depth >= this.target + this.avgPush) {
                this.started = true; this.fadeIn = this.fade; this.frac = 0;
                // A backlog far above the target is stale audio queued before the
                // engine began pulling: drop it now, not at the next quiet moment.
                var excess = this.depth - (this.target + this.avgPush);
                if (excess > this.skipExcess) { this.pendingSkip = excess; this._skip(); }
            } else { out.fill(0); this._tick(n); return; }
        }
        if (this.depth < this.periodMin) this.periodMin = this.depth;
        if (this.depth < n + this.reserve + 2) { this._underrun(out); this._tick(n); return; }
        if (this.pendingSkip > 0 && !this.xfadeLeft &&
            (this.energy < this.env * 0.25 || this.clock >= this.skipDeadline)) this._skip();

        var ring = this.ring, len = ring.length, rd = this.rd, frac = this.frac, rate = this.rate;
        var fade = this.fade, fadeIn = this.fadeIn, xl = this.xfadeLeft, xf = this.xfadeFrom;
        var consumed = 0, e = 0;
        for (var i = 0; i < n; i++) {
            var r1 = rd + 1; if (r1 === len) r1 = 0;
            var s = ring[rd] + (ring[r1] - ring[rd]) * frac;
            if (xl > 0) { var w = xl / fade; s = ring[xf] * w + s * (1 - w); xl--; if (++xf === len) xf = 0; }
            if (fadeIn > 0) { s *= 1 - fadeIn / fade; fadeIn--; }
            out[i] = s; e += s * s;
            frac += rate;
            while (frac >= 1) { frac -= 1; consumed++; if (++rd === len) rd = 0; }
        }
        this.rd = rd; this.frac = frac; this.depth -= consumed;
        this.fadeIn = fadeIn; this.xfadeLeft = xl; this.xfadeFrom = xf;
        this.energy = e; this.env += (e - this.env) * 0.005;
        this._tick(n);
    }

    // Drop everything and re-buffer; the learned target survives.
    flush() {
        this.rd = this.wr; this.depth = 0; this.frac = 0; this.started = false;
        this.fadeIn = 0; this.xfadeLeft = 0; this.pendingSkip = 0;
        this.lastPush = -1; this.underrunOpen = false; this.energy = 0;
    }

    stats() {
        var ms = this.ms;
        return {
            bufMs: (this.target + this.avgPush) / ms,   // what a fresh start buffers
            targetMs: this.target / ms,
            depthMs: this.depth / ms,
            floorMs: this.periodMin === Infinity ? this.depth / ms : this.periodMin / ms,
            rate: this.rate, holdS: this.hold * this.period / (1000 * ms),
            underruns: this.underruns, skips: this.skips, overflows: this.overflows,
            locked: !!this.lock, started: this.started
        };
    }

    _tick(n) {
        this.clock += n; this.periodPos += n;
        if (this.periodPos >= this.period) this._endPeriod();
    }

    _emit(type, extra) {
        if (!this.onEvent) return;
        var ev = this.stats(); ev.type = type;
        for (var k in extra) ev[k] = extra[k];
        this.onEvent(ev);
    }

    _underrun(out) {
        var n = out.length, ring = this.ring, len = ring.length, rd = this.rd, k = Math.min(this.depth, n), i;
        for (i = 0; i < k; i++) { out[i] = ring[rd] * (1 - i / k); if (++rd === len) rd = 0; }
        for (; i < n; i++) out[i] = 0;
        this.rd = this.wr; this.depth = 0; this.frac = 0;
        this.fadeIn = 0; this.xfadeLeft = 0; this.pendingSkip = 0; this.energy = 0;
        this.started = false; this.underruns++; this.underrunOpen = true;
        this.periodMin = Infinity;             // the trough before the underrun is not this period's floor
        // A repeat within twice the hold doubles it: periodic stalls stay covered.
        var since = this.lastUnderrun < 0 ? Infinity : this.clock - this.lastUnderrun;
        this.hold = since < 2 * this.hold * this.period ? Math.min(this.hold * 2, this.holdMax) : this.holdMin;
        this.lastUnderrun = this.clock;
    }

    // First frame after an underrun: the gap says how much buffer a repeat needs.
    _grow(gap) {
        var needed = gap - this.avgPush + this.reserve + this.safety, from = this.target;
        // The gap is measured in consumer blocks: always grow by at least one.
        if (needed < this.target + this.block) needed = this.target + this.block;
        if (!this.lock) this.target = this._clampTarget(Math.round(needed));
        this._emit('underrun', { gapMs: gap / this.ms, fromMs: from / this.ms, toMs: this.target / this.ms });
    }

    _skip() {
        var k = this.pendingSkip, maxK = this.depth - this.fade - this.reserve - 256;
        this.pendingSkip = 0;
        if (k > maxK) k = maxK;
        if (k <= 0) return;
        this.xfadeFrom = this.rd; this.xfadeLeft = this.fade;
        this.rd = (this.rd + k) % this.ring.length; this.depth -= k; this.skips++;
        this._emit('skip', { ms: k / this.ms });
    }

    _endPeriod() {
        this.periodPos -= this.period;
        if (this.lock) this._applyLock();
        // The stall budget this period needed, remembered for `hold` periods.
        var needed = this.periodGap ? this.periodGap - this.avgPush + this.reserve + this.safety : 0;
        this.hist[this.histPos] = needed; this.histPos = (this.histPos + 1) % this.hist.length;
        if (this.histLen < this.hist.length) this.histLen++;
        if (!this.lock) {
            var k = Math.min(this.histLen, this.hold), want = this.minTarget, hl = this.hist.length;
            for (var i = 1; i <= k; i++) { var v = this.hist[(this.histPos - i + hl) % hl]; if (v > want) want = v; }
            want = Math.min(want, this.maxTarget);
            if (want < this.target) this.target = Math.round(want);
        }
        // Steer the floor onto the target: resample a little, skip a lot.
        if (this.started && this.periodMin !== Infinity) {
            var err = (this.periodMin - this.target) / this.ms;
            if (this.periodMin - this.target > this.skipExcess) {
                if (!this.pendingSkip) this.skipDeadline = this.clock + Math.round(1500 * this.ms);
                this.pendingSkip = this.periodMin - this.target;
                this.rate = 1;
            } else {
                var r = err * 0.0002;
                this.rate = 1 + (r > 0.02 ? 0.02 : r < -0.004 ? -0.004 : r);
            }
        } else this.rate = 1;
        this._emit('stats', {});
        this.periodMin = Infinity; this.periodGap = 0;
    }
}

// The worklet is built from source text (as recorder.js does): the class
// above plus the processor shell below, so both builds load one file and the
// standalone bundle needs no runtime URL to fingerprint.
function wfRxWorkletMain() {
    class WfwebAudioProcessor extends AudioWorkletProcessor {
        constructor(options) {
            super();
            var jb = this.jb = new WfJitterBuffer(sampleRate, options && options.processorOptions);
            var port = this.port;
            jb.onEvent = function (ev) { port.postMessage(ev); };
            port.onmessage = function (e) {
                var d = e.data;
                if (d === 'flush') jb.flush();
                else if (d && d.cmd === 'lock') jb.setLock(d.ms);
                else jb.push(d);
            };
        }
        process(inputs, outputs) {
            var out = outputs[0] && outputs[0][0];
            if (out) this.jb.pull(out);
            return true;
        }
    }
    registerProcessor('wfweb-audio-processor', WfwebAudioProcessor);
}

function wfRxWorkletUrl() {
    var src = '"use strict";\n' + WfJitterBuffer.toString() + '\n(' + wfRxWorkletMain.toString() + ')();';
    return URL.createObjectURL(new Blob([src], { type: 'application/javascript' }));
}
