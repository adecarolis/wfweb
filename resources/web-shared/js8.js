// JS8 Web Worker harness — bridges the SPA's audio bus to the wasm
// codec exported from resources/web-standalone/wasm/js8.{mjs,wasm}.
//
// Loaded as a module by both the server SPA (resources/web/index.html)
// and the standalone SPA (resources/web-standalone/index.html). Both
// builds resolve "./wasm/js8.mjs" the same way thanks to the qrc alias
// in web.qrc and tools/build-static.sh's copy step.
//
// Phase 0 day 5: just establishes the loader and the public surface.
// Phase 1 fills in the slot-boundary scheduling + 48-to-12kHz
// downsampling + decode result rendering, mirroring the existing FT8
// integration in index.html.

let modulePromise = null;

// Public init: resolves to a thin handle exposing encode/decoder.
// Idempotent — repeated calls return the same module.
export async function js8Init() {
    if (modulePromise) return modulePromise;
    modulePromise = (async () => {
        const createJS8 = (await import("./wasm/js8.mjs")).default;
        const Module = await createJS8();
        return new JS8Module(Module);
    })();
    return modulePromise;
}

class JS8Module {
    constructor(M) {
        this.M = M;
        this._encode = M._js8_encode;
        this._decoder_new  = M._js8_decoder_new;
        this._decoder_free = M._js8_decoder_free;
        this._decoder_push = M._js8_decoder_push;
        this._decoder_run  = M._js8_decoder_run;
        this._decoder_pop  = M._js8_decoder_pop;
        this._free_string  = M._js8_free_string;
        this._malloc = M._malloc;
        this._free   = M._free;
    }

    // Encode a 12-character JS8 message of the given frame type
    // (0..7) and return Int32Array of 79 tones (each 0..7), or null
    // on bad input.
    encode(frameType, msg) {
        if (typeof msg !== "string" || msg.length !== 12) return null;
        const msgPtr = this._malloc(13);
        for (let i = 0; i < 12; i++) this.M.HEAPU8[msgPtr+i] = msg.charCodeAt(i);
        this.M.HEAPU8[msgPtr+12] = 0;
        const tonesPtr = this._malloc(79 * 4);
        const rc = this._encode(frameType, msgPtr, tonesPtr);
        let out = null;
        if (rc === 0) {
            out = new Int32Array(79);
            for (let i = 0; i < 79; i++) out[i] = this.M.HEAP32[tonesPtr/4 + i];
        }
        this._free(msgPtr);
        this._free(tonesPtr);
        return out;
    }

    // Create a Decoder object for the given submode (matching the
    // bits of Varicode::SubmodeType: Normal=0, Fast=1, Turbo=2,
    // Slow=4, Ultra=8).
    newDecoder(submode = 0) {
        const ptr = this._decoder_new(submode);
        if (!ptr) return null;
        return new JS8Decoder(this, ptr);
    }
}

class JS8Decoder {
    constructor(js8, ptr) {
        this.js8 = js8;
        this.ptr = ptr;
    }

    // Push 12 kHz mono Float32Array. Returns samples consumed.
    push(samples) {
        if (this.ptr === 0) return 0;
        const n = samples.length;
        const sPtr = this.js8._malloc(n * 4);
        new Float32Array(this.js8.M.HEAPF32.buffer, sPtr, n).set(samples);
        const consumed = this.js8._decoder_push(this.ptr, sPtr, n);
        this.js8._free(sPtr);
        return consumed;
    }

    // Run a decode pass. Returns the number of decoded messages
    // queued (drain via pop()).
    run() {
        if (this.ptr === 0) return 0;
        return this.js8._decoder_run(this.ptr);
    }

    // Pop one queued decoded message (object with snr/dt/freq/text/...
    // fields per the JSON format in api/js8_wasm_api.cpp), or null.
    pop() {
        if (this.ptr === 0) return null;
        const sPtr = this.js8._decoder_pop(this.ptr);
        if (!sPtr) return null;
        // UTF-8 read until NUL
        const u8 = this.js8.M.HEAPU8;
        let end = sPtr;
        while (u8[end] !== 0) ++end;
        const json = new TextDecoder().decode(u8.subarray(sPtr, end));
        this.js8._free_string(sPtr);
        try { return JSON.parse(json); } catch { return null; }
    }

    free() {
        if (this.ptr) {
            this.js8._decoder_free(this.ptr);
            this.ptr = 0;
        }
    }
}
