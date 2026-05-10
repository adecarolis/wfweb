// Day-4 gate smoke test: load js8.wasm, encode a JS8 message, verify
// the 79 tones include the Costas arrays at the right offsets and are
// all in [0,7]. Mirrors tools/js8-spike/test_encode_raw.mjs but against
// the production js8.wasm rather than the spike's standalone build.
//
// Run: node tools/test-js8-wasm.mjs

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
// Shim CJS-style globals that the Emscripten 3.1.6 ESM loader expects.
globalThis.__filename = __filename;
globalThis.__dirname  = __dirname;

const wasmDir  = path.join(__dirname, "..", "resources", "web-standalone", "wasm");
const wasmPath = path.join(wasmDir, "js8.wasm");
const wasmBytes = fs.readFileSync(wasmPath);

// Minimal stub imports — emcc emits a few wasi/env symbols even with
// -sENVIRONMENT=web,worker. We don't actually use any of them at the
// encoder code path; provide no-ops so instantiation succeeds.
let memory;
const heap = () => new Uint8Array(memory.buffer);
const heap32 = () => new Int32Array(memory.buffer);

const noop = () => 0;
const env = {
  __cxa_allocate_exception: () => 0,
  __cxa_begin_catch:        noop,
  __cxa_end_catch:          noop,
  __cxa_find_matching_catch_2: () => 0,
  __cxa_find_matching_catch_3: () => 0,
  __cxa_free_exception:     noop,
  __cxa_throw:              (ptr) => { throw new Error("C++ throw @"+ptr); },
  __resumeException:        () => { throw new Error("rethrow"); },
  emscripten_memcpy_big:    (dst, src, n) => { heap().copyWithin(dst, src, src+n); },
  emscripten_resize_heap:   () => 0,
  emscripten_get_now:       () => Date.now(),
  emscripten_get_now_is_monotonic: 1,
  getTempRet0:              () => 0,
  setTempRet0:              () => {},
  invoke_i:                 (idx) => instance.exports.__indirect_function_table.get(idx)(),
  invoke_ii:                (idx, a) => instance.exports.__indirect_function_table.get(idx)(a),
  invoke_iii:               (idx, a, b) => instance.exports.__indirect_function_table.get(idx)(a, b),
  invoke_iiii:              (idx, a, b, c) => instance.exports.__indirect_function_table.get(idx)(a, b, c),
  invoke_iiiii:             (idx, a, b, c, d) => instance.exports.__indirect_function_table.get(idx)(a, b, c, d),
  invoke_v:                 (idx) => instance.exports.__indirect_function_table.get(idx)(),
  invoke_vi:                (idx, a) => instance.exports.__indirect_function_table.get(idx)(a),
  invoke_vii:               (idx, a, b) => instance.exports.__indirect_function_table.get(idx)(a, b),
  invoke_viii:              (idx, a, b, c) => instance.exports.__indirect_function_table.get(idx)(a, b, c),
  invoke_viiii:             (idx, a, b, c, d) => instance.exports.__indirect_function_table.get(idx)(a, b, c, d),
  abort:                    () => { throw new Error("abort"); },
  clock_gettime:            () => 0,
  fd_close:                 () => 0,
  fd_seek:                  () => 0,
  fd_write:                 () => 0,
  proc_exit:                () => {},
  environ_sizes_get:        () => 0,
  environ_get:              () => 0,
};

// Use the Emscripten loader so the import-object gets wired correctly.
// Pass wasmBinary so the loader doesn't try to fetch() — Node 18's
// undici doesn't support file:// fetches and the encoder spike hit the
// same wall. Browsers don't have this problem.
const createJS8 = (await import(path.join(wasmDir, "js8.mjs"))).default;
const Module = await createJS8({ wasmBinary: wasmBytes });
memory = Module.HEAPU8.buffer ? new WebAssembly.Memory({ initial: 0 }) : null;
// Use Module.HEAPU8 / HEAP32 directly — they auto-track memory growth.
const heapU8 = () => Module.HEAPU8;
const heapI32 = () => Module.HEAP32;

const _js8_encode = Module._js8_encode;
const _malloc     = Module._malloc;
const _free       = Module._free;

let allOk = true;
const expectedCostas = "4,2,5,6,1,3,0";

for (const msg of ["HELLOWK1FMab", "CQK1FMEM85en"]) {
  const msgPtr = _malloc(13);
  for (let i = 0; i < 12; i++) heapU8()[msgPtr+i] = msg.charCodeAt(i);
  heapU8()[msgPtr+12] = 0;
  const tonesPtr = _malloc(79*4);
  const rc = _js8_encode(0, 0, msgPtr, tonesPtr);  // (submode=Normal, frameType=0)
  if (rc !== 0) {
    console.log(`encode("${msg}"): rc=${rc} FAIL`);
    allOk = false;
  } else {
    const tones = [];
    for (let i = 0; i < 79; i++) tones.push(heapI32()[tonesPtr/4+i]);
    const a = tones.slice(0, 7).join(",");
    const b = tones.slice(36, 43).join(",");
    const c = tones.slice(72, 79).join(",");
    const inRange = tones.every(t => t >= 0 && t <= 7);
    const ok = (a === expectedCostas && b === expectedCostas &&
                c === expectedCostas && inRange);
    console.log(`encode("${msg}"): ${ok ? "PASS" : "FAIL"}`);
    console.log(`  Costas A: ${a}`);
    console.log(`  Costas B: ${b}`);
    console.log(`  Costas C: ${c}`);
    console.log(`  parity:   ${tones.slice(7,37).join(",")}`);
    console.log(`  message:  ${tones.slice(43,72).join(",")}`);
    if (!ok) allOk = false;
  }
  _free(msgPtr); _free(tonesPtr);
}

// Decoder lifecycle smoke-test (per Phase 0 gate: js8_decoder_new/free
// link cleanly even if decode returns 0 results).
const _new      = Module._js8_decoder_new;
const _free_dec = Module._js8_decoder_free;
const _push     = Module._js8_decoder_push;

const dec = _new(0); // JS8CallNormal
console.log(`\ndecoder lifecycle: new=${dec ? "ok" : "FAIL"}`);
if (dec) {
  // Push a bit of zero audio — exercise the path, no decode expected.
  const samples = _malloc(1000 * 4);
  const f32 = new Float32Array(Module.HEAPF32.buffer, samples, 1000);
  for (let i = 0; i < 1000; i++) f32[i] = 0;
  const consumed = _push(dec, samples, 1000);
  console.log(`  push(1000 silence) -> consumed=${consumed}`);
  _free(samples);
  _free_dec(dec);
  console.log(`  free: ok`);
}

console.log(allOk ? "\nDAY 4 GATE: PASS" : "\nDAY 4 GATE: FAIL");
process.exit(allOk ? 0 : 1);
