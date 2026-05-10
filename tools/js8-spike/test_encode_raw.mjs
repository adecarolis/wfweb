// Bypass Emscripten's loader (it wants CJS require()); instantiate the
// .wasm directly and call _js8_encode by hand. The actual browser/worker
// integration won't have this problem because it uses fetch + the proper
// Emscripten path; this harness is just for the spike.

import fs from "fs";

const wasmBytes = fs.readFileSync("./js8_encode.wasm");

// Stub the C++ ABI imports the module needs
let memory;
const heap = () => new Uint8Array(memory.buffer);
const heap32 = () => new Int32Array(memory.buffer);

const noop = () => 0;
const abort = (msg) => { throw new Error("wasm abort: " + msg); };

const env = {
  __cxa_allocate_exception: (sz) => 0,  // we never throw — encoder is total for valid input
  __cxa_begin_catch:        noop,
  __cxa_end_catch:          noop,
  __cxa_find_matching_catch_2: () => 0,
  __cxa_find_matching_catch_3: () => 0,
  __cxa_free_exception:     noop,
  __cxa_throw:              (ptr) => { throw new Error("C++ exception thrown @"+ptr); },
  __resumeException:        () => { throw new Error("rethrow"); },
  emscripten_memcpy_big:    (dst, src, n) => { heap().copyWithin(dst, src, src+n); },
  emscripten_resize_heap:   () => 0,
  getTempRet0:              () => 0,
  invoke_iii:               (idx, a, b)         => instance.exports.__indirect_function_table.get(idx)(a, b),
  invoke_viiii:             (idx, a, b, c, d)   => instance.exports.__indirect_function_table.get(idx)(a, b, c, d),
};

const importObject = { env, wasi_snapshot_preview1: env };
const { instance } = await WebAssembly.instantiate(wasmBytes, importObject);
memory = instance.exports.memory;

// Call ctors
instance.exports.__wasm_call_ctors();

const _js8_encode = instance.exports.js8_encode;
const _malloc = instance.exports.malloc;
const _free = instance.exports.free;

function encode(msg) {
  if (msg.length !== 12) throw new Error("must be 12 chars");
  const msgPtr = _malloc(13);
  for (let i = 0; i < 12; i++) heap()[msgPtr+i] = msg.charCodeAt(i);
  heap()[msgPtr+12] = 0;
  const tonesPtr = _malloc(79*4);
  const rc = _js8_encode(0, msgPtr, tonesPtr);
  if (rc !== 0) { _free(msgPtr); _free(tonesPtr); return null; }
  const out = [];
  for (let i = 0; i < 79; i++) out.push(heap32()[tonesPtr/4 + i]);
  _free(msgPtr); _free(tonesPtr);
  return out;
}

const expected = "4,2,5,6,1,3,0";
let allOk = true;
// JS8 alphabet has no space — use only [0-9A-Za-z-+], 12 chars exactly
for (const msg of ["HELLOWK1FMab", "CQK1FMEM85en", "AB12345-+Zaz"]) {
  const tones = encode(msg);
  if (!tones) { console.log(`encode("${msg}") FAILED`); allOk = false; continue; }

  const a = tones.slice(0, 7).join(",");
  const b = tones.slice(36, 43).join(",");
  const c = tones.slice(72, 79).join(",");
  const inRange = tones.every(t => t >= 0 && t <= 7);
  const ok = (a === expected && b === expected && c === expected && inRange);
  console.log(`encode("${msg}"): ${ok ? "PASS" : "FAIL"}`);
  console.log(`  Costas A:        ${a}  (expect ${expected})`);
  console.log(`  Costas B:        ${b}`);
  console.log(`  Costas C:        ${c}`);
  console.log(`  parity[7..36]:   ${tones.slice(7,37).join(",")}`);
  console.log(`  message[43..72]: ${tones.slice(43,72).join(",")}`);
  console.log(`  all tones [0,7]: ${inRange}`);
  if (!ok) allOk = false;
  console.log();
}
console.log(allOk ? "\nSPIKE PASSED" : "\nSPIKE FAILED");
process.exit(allOk ? 0 : 1);
