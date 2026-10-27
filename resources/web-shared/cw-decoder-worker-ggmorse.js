/// <reference lib="webworker" />
// CW Decoder Web Worker - ggmorse (Goertzel algorithm, pure signal processing).
//
// TEMPORARY comparison decoder: the page runs it next to the DeepCW worker
// (COMPARE_GGMORSE in cw-decoder.js) and shows its copy on a second line.
// The page taps the audio at 3200 Hz for DeepCW; ggmorse's resampler
// rejects rates under 4000 Hz, so this worker upsamples its copy 4:5 by
// linear interpolation before queuing it.

// Cache-bust: inherit ?v=… from this worker's own URL (set by parent at construction).
importScripts('ggmorse-wasm.js' + location.search);

let module = null;
let ggmorse_init_fn = null;
let ggmorse_queue_fn = null;
let ggmorse_decode_fn = null;
let ggmorse_get_text_ptr_fn = null;
let ggmorse_get_frequency_fn = null;
let ggmorse_get_speed_fn = null;

// 3200 -> 4000 Hz: five output samples per four input samples, linear.
let upLast = 0;          // last input sample of the previous chunk
let upPhase = 0;         // output position relative to that sample, in input samples
function upsample3200to4000(inp) {
    const ratio = 3200 / 4000;
    const out = new Float32Array(Math.ceil((inp.length - upPhase) / ratio) + 1);
    let n = 0;
    let pos = upPhase;   // position in input samples; -1 .. inp.length-1 is between upLast and inp
    while (pos <= inp.length - 1) {
        const i = Math.floor(pos);
        const frac = pos - i;
        const a = i < 0 ? upLast : inp[i];
        const b = i + 1 < inp.length ? inp[i + 1] : inp[inp.length - 1];
        out[n++] = a + (b - a) * frac;
        pos += ratio;
    }
    upPhase = pos - inp.length;      // carry the fractional position into the next chunk
    upLast = inp[inp.length - 1];
    return out.subarray(0, n);
}

self.onmessage = async (e) => {
    const msg = e.data;

    try {
        if (msg.type === 'loadModel') {
            // Initialize the WASM module
            module = await GGMorseModule();

            // Wrap exported functions
            ggmorse_init_fn        = module.cwrap('ggmorse_init',        null,     ['number']);
            ggmorse_queue_fn       = module.cwrap('ggmorse_queue',       null,     ['number', 'number']);
            ggmorse_decode_fn      = module.cwrap('ggmorse_decode',      'number', []);
            ggmorse_get_text_ptr_fn = module.cwrap('ggmorse_get_text',   'number', []);
            ggmorse_get_frequency_fn = module.cwrap('ggmorse_get_frequency', 'number', []);
            ggmorse_get_speed_fn   = module.cwrap('ggmorse_get_speed',   'number', []);

            // Initialize with 4000 Hz input sample rate (= ggmorse kBaseSampleRate, no resampling)
            ggmorse_init_fn(4000.0);

            console.log('[CW Decoder Worker] ggmorse initialized');
            self.postMessage({ type: 'modelLoaded' });

        } else if (msg.type === 'runInference') {
            if (!module) {
                self.postMessage({ type: 'error', error: 'Module not loaded' });
                return;
            }

            const audioBuffer = upsample3200to4000(msg.audioBuffer);
            const n = audioBuffer.length;
            if (n === 0) {
                self.postMessage({ type: 'inferenceResult', segments: [], frequency_hz: 0, speed_wpm: 0 });
                return;
            }

            // Copy samples into WASM heap
            const ptr = module._malloc(n * 4);
            module.HEAPF32.set(audioBuffer, ptr >> 2);

            // Queue samples and run decode
            ggmorse_queue_fn(ptr, n);
            module._free(ptr);
            ggmorse_decode_fn();

            // Read results
            const textPtr = ggmorse_get_text_ptr_fn();
            const text = module.UTF8ToString(textPtr);
            const frequency_hz = ggmorse_get_frequency_fn();
            const speed_wpm = ggmorse_get_speed_fn();

            const segments = text.length > 0
                ? [{ text: text, isAbbreviation: false }]
                : [];

            self.postMessage({ type: 'inferenceResult', segments, frequency_hz, speed_wpm });

        } else {
            self.postMessage({ type: 'error', error: 'Unknown message type' });
        }
    } catch (err) {
        self.postMessage({ type: 'error', error: err.message || String(err) });
    }
};

console.log('[CW Decoder Worker] Initialized (ggmorse)');
