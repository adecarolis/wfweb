// SSTV codec loader: the `sstv` Rust crate (resources/sstv-shim/) compiled to
// sstv.wasm, driven through its plain C ABI. Used by
// resources/web-shared/sstv-worker.js in both builds and by the node tests.
//
// RX is pull-based inside the crate and Asyncified at build time
// (tools/build-sstv-wasm.sh): decodeNext() returns a Promise that settles
// when the image ends; while it runs, the decoder asks for audio through the
// `awaitSamples` callback whenever the ring is empty, and pushRx() feeds it.

import { Asyncify } from './asyncify.mjs';

const WASM_URL = new URL('./sstv.wasm', import.meta.url);

// Display names by the crate's Debug key (its `modes::ALL` order).
const DISPLAY_NAMES = {
    Scottie1: 'Scottie 1', Scottie2: 'Scottie 2', ScottieDx: 'Scottie DX',
    Martin1: 'Martin 1', Martin2: 'Martin 2', Robot36: 'Robot 36', Robot72: 'Robot 72',
    WrasseSc2180: 'Wrasse SC2-180', PasokonP3: 'Pasokon P3', PasokonP5: 'Pasokon P5',
    PasokonP7: 'Pasokon P7', Pd50: 'PD-50', Pd90: 'PD-90', Pd120: 'PD-120', Pd160: 'PD-160',
    Pd180: 'PD-180', Pd240: 'PD-240', Pd290: 'PD-290',
};
const UNWIND_BUFFER = 64 * 1024;
const TX_CHUNK = 65536;

/**
 * Load the codec. `wasmBytes` (ArrayBuffer / Uint8Array) bypasses the fetch
 * for node; the browser fetches WASM_URL.
 */
export async function loadSstvCodec(opts = {}) {
    const asyncify = new Asyncify();
    const hooks = { awaitSamples: null, onMode: null, onRow: null };
    let memory = null;
    const u8 = () => new Uint8Array(memory.buffer);

    const imports = {
        env: {
            sstv_await_samples: asyncify.wrapImport(() => {
                if (!hooks.awaitSamples) return 1;
                return hooks.awaitSamples();
            }),
            sstv_on_mode: (id, w, h) => { if (hooks.onMode) hooks.onMode(id, w, h); },
            sstv_on_row: (index, ptr, len) => {
                if (hooks.onRow) hooks.onRow(index, u8().slice(ptr, ptr + len));
            },
        },
    };

    let instance;
    if (opts.wasmBytes) {
        ({ instance } = await WebAssembly.instantiate(opts.wasmBytes, imports));
    } else {
        const resp = await fetch(WASM_URL, { credentials: 'same-origin' });
        if (!resp.ok) throw new Error('sstv.wasm: HTTP ' + resp.status);
        if (WebAssembly.instantiateStreaming && resp.headers.get('content-type') === 'application/wasm') {
            ({ instance } = await WebAssembly.instantiateStreaming(resp, imports));
        } else {
            ({ instance } = await WebAssembly.instantiate(await resp.arrayBuffer(), imports));
        }
    }
    const ex = instance.exports;
    memory = ex.memory;
    asyncify.attach(instance, ex.sstv_alloc, UNWIND_BUFFER);
    const decodeAsync = asyncify.wrapExport(ex.sstv_rx_decode);

    // --- modes -------------------------------------------------------------
    const nameBuf = ex.sstv_alloc(64);
    const dec = new TextDecoder();
    const modes = [];
    for (let id = 0; id < ex.sstv_mode_count(); id++) {
        const n = ex.sstv_mode_name(id, nameBuf, 64);
        const key = dec.decode(u8().subarray(nameBuf, nameBuf + n));
        modes.push({
            id,
            key,
            name: DISPLAY_NAMES[key] || key,
            w: ex.sstv_mode_width(id),
            h: ex.sstv_mode_height(id),
        });
    }

    // --- scratch buffer for pushRx / tx ------------------------------------
    let scratch = { ptr: 0, bytes: 0 };
    function scratchFor(bytes) {
        if (scratch.bytes < bytes) {
            if (scratch.ptr) ex.sstv_free(scratch.ptr, scratch.bytes);
            scratch = { ptr: ex.sstv_alloc(bytes), bytes };
            if (!scratch.ptr) throw new Error('sstv: out of memory');
        }
        return scratch.ptr;
    }

    return {
        modes,
        modeByName(name) {
            const norm = s => String(s).toLowerCase().replace(/[^a-z0-9]/g, '');
            const key = norm(name);
            return modes.find(m => norm(m.name) === key || norm(m.key) === key) || null;
        },
        /** Transmission length in samples at `rate`, header included. */
        modeSamples(id, rate) { return ex.sstv_mode_samples(id, rate); },
        /** Length of the header (VOX tones, leader, VIS) in samples at `rate`. */
        modeHeaderSamples(id, rate) { return ex.sstv_mode_header_samples(id, rate); },

        /** Append Int16 samples to the RX ring. */
        pushRx(int16) {
            if (!int16 || !int16.length) return;
            const ptr = scratchFor(int16.length * 2);
            new Int16Array(memory.buffer, ptr, int16.length).set(int16);
            ex.sstv_rx_push(ptr, int16.length);
        },
        /** Append `n` samples of silence. */
        pushGap(n) {
            n = n | 0;
            if (n <= 0) return;
            const ptr = scratchFor(n * 2);
            new Int16Array(memory.buffer, ptr, n).fill(0);
            ex.sstv_rx_push(ptr, n);
        },
        resetRx() { ex.sstv_rx_reset(); },
        rxPending() { return ex.sstv_rx_pending(); },

        /**
         * Decode the next image. `pin` is a mode id or -1 (detect from the
         * VIS header); with `headerless` the image data is taken to start at
         * the next sample, in `pin` or Robot 36 (a start by hand, no header
         * search). `cb.awaitSamples()` is called when the ring runs dry
         * and must return a Promise of 0 (retry) or 1 (abort); `cb.onMode(id,
         * w, h)` once; `cb.onRow(index, Uint8Array rgb)` per row. Resolves 1
         * complete, 0 cut short, -1 no image found.
         */
        async decodeNext(rate, pin, cb, headerless) {
            hooks.awaitSamples = cb.awaitSamples;
            hooks.onMode = cb.onMode || null;
            hooks.onRow = cb.onRow || null;
            try {
                return await decodeAsync(rate >>> 0, pin | 0, headerless ? 1 : 0);
            } finally {
                hooks.awaitSamples = hooks.onMode = hooks.onRow = null;
            }
        },

        /**
         * Synthesize `rgb` (Uint8Array, w*h*3 at the mode's resolution) as
         * Int16 samples at `rate`. Synchronous.
         */
        encode(modeId, rgb, rate) {
            const mode = modes[modeId];
            if (!mode) throw new Error('sstv: unknown mode id ' + modeId);
            const need = mode.w * mode.h * 3;
            if (!rgb || rgb.length !== need) throw new Error('sstv: image must be ' + mode.w + 'x' + mode.h + ' RGB');
            const inPtr = ex.sstv_alloc(need);
            u8().set(rgb, inPtr);
            let total;
            try {
                total = ex.sstv_tx_begin(modeId, inPtr, rate >>> 0);
            } finally {
                ex.sstv_free(inPtr, need);
            }
            if (!total) throw new Error('sstv: encoder refused the image');
            const out = new Int16Array(total);
            const chunkPtr = scratchFor(TX_CHUNK * 2);
            let pos = 0;
            for (;;) {
                const n = ex.sstv_tx_fill(chunkPtr, Math.min(TX_CHUNK, total - pos));
                if (!n) break;
                out.set(new Int16Array(memory.buffer, chunkPtr, n), pos);
                pos += n;
                if (pos >= total) break;
            }
            ex.sstv_tx_end();
            return { samples: pos === total ? out : out.subarray(0, pos), seconds: total / rate };
        },
    };
}
