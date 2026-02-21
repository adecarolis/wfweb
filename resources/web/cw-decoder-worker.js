/// <reference lib="webworker" />
// CW Decoder Web Worker - ggmorse (Goertzel algorithm, pure signal processing)

importScripts('ggmorse-wasm.js');

let module = null;
let ggmorse_init_fn = null;
let ggmorse_queue_fn = null;
let ggmorse_decode_fn = null;
let ggmorse_get_text_ptr_fn = null;
let ggmorse_get_frequency_fn = null;
let ggmorse_get_speed_fn = null;

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

            const audioBuffer = msg.audioBuffer; // Float32Array, 3200 Hz
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
