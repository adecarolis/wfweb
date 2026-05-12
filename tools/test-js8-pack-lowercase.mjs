// Regression: lowercase free-text used to spin buildMessageFrames forever
// because JSC's dictionary is uppercase-only. Watchdog must bail out
// instead of hanging. We give the call 5 s; if it takes longer the test
// will be killed by node's own timer.

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
globalThis.__filename = fileURLToPath(import.meta.url);
globalThis.__dirname  = __dirname;

const wasmDir   = path.join(__dirname, "..", "resources", "web-standalone", "wasm");
const wasmBytes = fs.readFileSync(path.join(wasmDir, "js8.wasm"));
const createJS8 = (await import(path.join(wasmDir, "js8.mjs"))).default;
const Module    = await createJS8({ wasmBinary: wasmBytes });

function pack(mycall, mygrid, sel, text, submode = 0) {
    const enc = (s) => {
        const b = Buffer.from(s + "\0", "utf8");
        const p = Module._malloc(b.length);
        Module.HEAPU8.set(b, p);
        return p;
    };
    const a = enc(mycall), c = enc(mygrid), d = enc(sel), e = enc(text);
    const out = Module._js8_pack(a, c, d, e, submode);
    Module._free(a); Module._free(c); Module._free(d); Module._free(e);
    if (!out) return null;
    let end = out;
    while (Module.HEAPU8[end] !== 0) ++end;
    const json = new TextDecoder().decode(Module.HEAPU8.subarray(out, end));
    Module._js8_free_string(out);
    try { return JSON.parse(json); } catch { return null; }
}

// 5 s watchdog: if pack hangs, exit non-zero.
const watchdog = setTimeout(() => {
    console.error("FAIL — pack() did not return within 5 s (watchdog still missing or broken)");
    process.exit(1);
}, 5000);

console.log("Test 1: lowercase text — used to hang");
const t1 = Date.now();
const out1 = pack("K1FM", "EM85", "", "hello", 0);
const dt1 = Date.now() - t1;
console.log(`  → ${out1 === null ? "null" : JSON.stringify(out1)} in ${dt1} ms`);

console.log("Test 2: uppercase text — should pack normally");
const t2 = Date.now();
const out2 = pack("K1FM", "EM85", "", "HELLO", 0);
const dt2 = Date.now() - t2;
console.log(`  → ${out2 ? out2.length : 0} frame(s) in ${dt2} ms`);

console.log("Test 3: mixed case — watchdog should kick in if mixed isn't normalized");
const t3 = Date.now();
const out3 = pack("K1FM", "EM85", "", "Hello World", 0);
const dt3 = Date.now() - t3;
console.log(`  → ${out3 ? out3.length : 0} frame(s) in ${dt3} ms`);

console.log("Test 4: garbage punctuation only — must not hang");
const t4 = Date.now();
const out4 = pack("K1FM", "EM85", "", "!!!???", 0);
const dt4 = Date.now() - t4;
console.log(`  → ${out4 ? out4.length : 0} frame(s) in ${dt4} ms`);

clearTimeout(watchdog);
const allFast = [dt1, dt2, dt3, dt4].every(d => d < 1000);
console.log("");
console.log(allFast ? "WATCHDOG GATE: PASS — no pack() call exceeded 1 s"
                    : "WATCHDOG GATE: FAIL — at least one pack() took > 1 s");
process.exit(allFast && out2 && out2.length > 0 ? 0 : 1);
