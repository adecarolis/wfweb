// Drives resources/web-shared/rx-jitter.js through synthetic arrival patterns
// (jitter, stalls, clock drift, TX gaps) and checks that underruns stop once
// the buffer has adapted, that latency comes back down afterwards, and that
// the output never has a hard discontinuity. Run: node tests/rx_jitter_sim.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const src = fs.readFileSync(path.join(here, '../resources/web-shared/rx-jitter.js'), 'utf8');
const WfJitterBuffer = new Function(src + '\nreturn WfJitterBuffer;')();

const SR = 48000;

// A seeded PRNG so a failure reproduces.
function rng(seed) {
    let s = seed >>> 0;
    return () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; };
}

// opts: seconds, frameMs, quantum, ratio (rig clock / sound-card clock),
// delay(k, tGenMs) -> extra wall ms before frame k arrives,
// flushAt(tMs) -> true to flush at that wall time (TX),
// stopped(tMs) -> true while the rig sends nothing (TX),
// jbOpts, lockAt {t, ms}
function simulate(opts) {
    const frameMs = opts.frameMs || 20, q = opts.quantum || 128, ratio = opts.ratio || 1;
    const frameLen = Math.round(frameMs * SR / 1000);
    const jb = new WfJitterBuffer(SR, opts.jbOpts || {});
    const events = [];
    jb.onEvent = ev => events.push(Object.assign({ t: tMs }, ev));
    const out = new Float32Array(q);
    let tMs = 0, k = 0, lastArrival = 0, genSample = 0;
    let maxDiff = 0, prev = 0, started = false, zeroAfterStart = 0, rendered = 0, lastFlush = -1e9;
    const trace = [];
    const totalMs = opts.seconds * 1000;
    const pending = [];   // frames generated but not yet arrived: {at, data}
    // Frames queued before the engine pulled for the first time (a late
    // AudioContext start): pushed up front, all at once.
    for (let b = 0; b < (opts.backlogMs || 0); b += frameMs) {
        const data = new Float32Array(frameLen);
        for (let i = 0; i < frameLen; i++) data[i] = 0.5 * Math.sin(2 * Math.PI * 1000 * (genSample + i) / SR);
        genSample += frameLen;
        jb.push(data);
    }
    while (tMs < totalMs) {
        // generate frames whose rig time has come
        while (k * frameMs * ratio <= tMs) {
            const tGen = k * frameMs * ratio;
            const data = new Float32Array(frameLen);
            for (let i = 0; i < frameLen; i++) data[i] = 0.5 * Math.sin(2 * Math.PI * 1000 * (genSample + i) / SR);
            genSample += frameLen;
            const stopped = opts.stopped && opts.stopped(tGen);
            if (!stopped) {
                let at = tGen + (opts.delay ? opts.delay(k, tGen) : 0);
                if (at < lastArrival) at = lastArrival;
                lastArrival = at;
                pending.push({ at, data });
            }
            k++;
        }
        while (pending.length && pending[0].at <= tMs) jb.push(pending.shift().data);
        if (opts.flushAt && opts.flushAt(tMs) && tMs - lastFlush > 1000) { jb.flush(); lastFlush = tMs; prev = 0; }
        if (opts.lockAt && Math.abs(tMs - opts.lockAt.t) < q / SR * 1000 / 2) jb.setLock(opts.lockAt.ms);
        jb.pull(out);
        if (jb.started) started = true;
        if (started) {
            rendered += q;
            for (let i = 0; i < q; i++) {
                const d = Math.abs(out[i] - prev); if (d > maxDiff) maxDiff = d; prev = out[i];
                if (out[i] === 0) zeroAfterStart++;
            }
        }
        tMs += q * 1000 / SR;
        if (trace.length === 0 || tMs - trace[trace.length - 1].ms >= 10000) trace.push({ ms: tMs, t: Math.round(tMs / 1000), buf: Math.round(jb.stats().bufMs), u: jb.underruns });
    }
    return { jb, events, maxDiff, trace, silence: zeroAfterStart / rendered, stats: jb.stats() };
}

let failures = 0;
function check(name, cond, detail) {
    console.log((cond ? 'PASS ' : 'FAIL ') + name + (detail ? '  (' + detail + ')' : ''));
    if (!cond) failures++;
}
function fmt(r) {
    return 'underruns=' + r.jb.underruns + ' skips=' + r.jb.skips + ' buf=' + r.stats.bufMs.toFixed(0) + 'ms maxDiff=' + r.maxDiff.toFixed(3) +
        ' silence=' + (r.silence * 100).toFixed(2) + '% trace=' + r.trace.map(p => p.t + 's:' + p.buf + '/' + p.u).join(' ');
}
const CLICK = 0.07; // 1 kHz sine at 0.5 has a max sample step of 0.065

// 1. A clean stream: no underruns, latency stays at the minimum.
{
    const r = simulate({ seconds: 60 });
    check('steady: no underruns', r.jb.underruns === 0, fmt(r));
    check('steady: buffer at minimum', r.stats.bufMs < 45, fmt(r));
    check('steady: no clicks', r.maxDiff < CLICK);
}

// 2. Random jitter 0..30 ms per frame: a bump or two to learn it, then none.
{
    const rnd = rng(1);
    const r = simulate({ seconds: 120, delay: () => rnd() * 30 });
    const late = r.events.filter(e => e.type === 'underrun' && e.t > 20000).length;
    check('jitter 30ms: learns within 20 s', late === 0 && r.jb.underruns <= 3, fmt(r));
    check('jitter 30ms: buffer under 90 ms', r.stats.bufMs < 90, fmt(r));
    check('jitter 30ms: no clicks', r.maxDiff < CLICK);
}

// 3. A 150 ms stall every 15 s: the first costs a bump, the rest are absorbed.
{
    const r = simulate({ seconds: 300, delay: (k, t) => (t % 15000) < 20 && t > 0 ? 150 : 0 });
    // Frames generated right after the stall point are held with it: emulate a burst.
    const r2 = simulate({ seconds: 300, delay: (k, t) => { const ph = t % 15000; return (ph < 150 && t > 1000) ? 150 - ph : 0; } });
    const late = r2.events.filter(e => e.type === 'underrun' && e.t > 40000).length;
    check('periodic 150ms stall: at most 2 bumps, none after 40 s', r2.jb.underruns <= 2 && late === 0, fmt(r2));
    check('periodic 150ms stall: buffer sized to the stall', r2.stats.bufMs > 150 && r2.stats.bufMs < 230 && r2.stats.depthMs < 260, fmt(r2) + ' depth=' + r2.stats.depthMs.toFixed(0));
    check('periodic 150ms stall: no clicks', r2.maxDiff < CLICK, fmt(r));
}

// 4. One 400 ms stall, then calm: one bump, latency back down within ~90 s.
{
    const r = simulate({ seconds: 200, delay: (k, t) => (t >= 20000 && t < 20400) ? 20400 - t : 0 });
    check('one-off 400ms stall: exactly one bump', r.jb.underruns === 1, fmt(r));
    const grown = r.trace.find(p => p.t === 30);
    check('one-off 400ms stall: buffer grew to cover it', grown && grown.buf > 400, fmt(r));
    check('one-off 400ms stall: back under 50 ms by 200 s', r.stats.bufMs < 50 && r.stats.depthMs < 60, fmt(r) + ' depth=' + r.stats.depthMs.toFixed(0));
    check('one-off 400ms stall: skipped the excess once', r.jb.skips >= 1 && r.jb.skips <= 3, fmt(r));
    check('one-off 400ms stall: no clicks', r.maxDiff < CLICK);
}

// 5/6. Clock drift ±200 ppm for 10 minutes: resampling absorbs it, no bumps, no growth.
{
    const r = simulate({ seconds: 600, ratio: 1.0002 });
    check('drift +200ppm (buffer drains): no underruns', r.jb.underruns === 0, fmt(r));
    check('drift +200ppm: no clicks', r.maxDiff < CLICK);
    const r2 = simulate({ seconds: 600, ratio: 0.9998 });
    check('drift -200ppm (buffer fills): no underruns, no skips', r2.jb.underruns === 0 && r2.jb.skips === 0, fmt(r2));
    check('drift -200ppm: latency did not grow', r2.stats.depthMs < 70, fmt(r2));
}

// 7. TX every 30 s: flush + 5 s of no frames. Not jitter: the buffer must not grow.
{
    const r = simulate({ seconds: 180, flushAt: t => t >= 30000 && (t % 30000) < 3, stopped: t => t >= 30000 && (t % 30000) < 5000 });
    check('TX gaps: no underruns', r.jb.underruns === 0, fmt(r));
    check('TX gaps: buffer stayed at minimum', r.stats.bufMs < 45, fmt(r));
}

// 8. Pinned to 120 ms: holds there through jitter, never adapts.
{
    const rnd = rng(7);
    const r = simulate({ seconds: 60, jbOpts: { lockMs: 120 }, delay: () => rnd() * 30 });
    check('lock 120ms: stays at 120 ms', Math.abs(r.stats.bufMs - 120) < 2 && r.stats.locked, fmt(r));
    check('lock 120ms: no underruns', r.jb.underruns === 0, fmt(r));
}

// 9. A restored target from a previous session is honoured for a while.
{
    const r = simulate({ seconds: 30, jbOpts: { targetMs: 150 } });
    const at10 = r.trace.find(p => p.t === 10);
    check('restored 150ms: still held at 10 s', at10 && at10.buf > 150, fmt(r));
}

// 10. ScriptProcessor-sized blocks (4096): the floor rises to fit a block, no underruns.
{
    const rnd = rng(3);
    const r = simulate({ seconds: 60, quantum: 4096, delay: () => rnd() * 20 });
    check('4096 blocks: no underruns after 10 s', r.events.filter(e => e.type === 'underrun' && e.t > 10000).length === 0, fmt(r));
    check('4096 blocks: no clicks', r.maxDiff < CLICK);
}

// 11. A 1.2 s outage with a burst afterwards: growth capped at 1 s, then recovery.
{
    const r = simulate({ seconds: 240, delay: (k, t) => (t >= 30000 && t < 31200) ? 31200 - t : 0 });
    check('1.2s outage: one bump', r.jb.underruns === 1, fmt(r));
    const grown = r.trace.find(p => p.t === 40);
    check('1.2s outage: buffer grew to the 1 s cap', grown && grown.buf >= 1000 && r.trace.every(p => p.buf <= 1100), fmt(r));
    check('1.2s outage: recovered under 50 ms', r.stats.bufMs < 50 && r.stats.depthMs < 60, fmt(r) + ' depth=' + r.stats.depthMs.toFixed(0));
    check('1.2s outage: no clicks', r.maxDiff < CLICK);
}

// 12. A stream that stops for longer than any buffer (3 s) is not jitter: no growth.
{
    const r = simulate({ seconds: 120, delay: (k, t) => (t >= 30000 && t < 33000) ? 33000 - t : 0 });
    check('3s outage: treated as a stopped stream, no growth', r.trace.every(p => p.buf <= 45), fmt(r));
    check('3s outage: late burst skipped away', r.stats.depthMs < 60, fmt(r) + ' depth=' + r.stats.depthMs.toFixed(0));
}

// 13. 3 s of frames queued before the first pull (late audio start): dropped at once.
{
    const r = simulate({ seconds: 60, backlogMs: 3000 });
    const skip = r.events.find(e => e.type === 'skip');
    check('startup backlog: skipped on the first pull', skip && skip.t < 20 && skip.ms > 2800, fmt(r) + (skip ? ' skipAt=' + skip.t.toFixed(0) + 'ms' : ''));
    check('startup backlog: one skip, no underruns, live afterwards', r.jb.skips === 1 && r.jb.underruns === 0 && r.stats.depthMs < 60, fmt(r) + ' depth=' + r.stats.depthMs.toFixed(0));
    check('startup backlog: no clicks', r.maxDiff < CLICK);
}

console.log(failures ? failures + ' FAILED' : 'all passed');
process.exit(failures ? 1 : 0);
