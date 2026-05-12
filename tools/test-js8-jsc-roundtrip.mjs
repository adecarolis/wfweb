// Tier 2 gate: free-text JSC dictionary roundtrip.
//
// Demonstrates that the JSC (English Huffman) dictionary is wired up
// end-to-end:
//
//   pack "HELLO WORLD" → buildMessageFrames → N×12-char raw frames
//   for each frame: js8_encode → synth → push
//   js8_decoder_run → drain pop → concatenate decoded text
//   verify the decoded text contains the original phrase.
//
// Phase-0 stub would produce empty text here. With Tier 2 vendored
// JSC the decoder reconstructs the words from the dictionary.
//
// Run: node tools/test-js8-jsc-roundtrip.mjs

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
globalThis.__filename = __filename;
globalThis.__dirname  = __dirname;

const wasmDir   = path.join(__dirname, "..", "resources", "web-standalone", "wasm");
const wasmBytes = fs.readFileSync(path.join(wasmDir, "js8.wasm"));
const createJS8 = (await import(path.join(wasmDir, "js8.mjs"))).default;
const Module    = await createJS8({ wasmBinary: wasmBytes });

const sharedJs8  = await import(path.join(__dirname, "..", "resources", "web-shared", "js8.mjs"));
const synthesize = sharedJs8.synthesize;

const SAMPLE_RATE = 12000;

function pack(mycall, mygrid, selectedCall, text, submode = 0) {
    const enc = (s) => {
        const b = Buffer.from(s + "\0", "utf8");
        const p = Module._malloc(b.length);
        Module.HEAPU8.set(b, p);
        return p;
    };
    const pMycall = enc(mycall);
    const pMygrid = enc(mygrid);
    const pSel    = enc(selectedCall);
    const pText   = enc(text);
    const out = Module._js8_pack(pMycall, pMygrid, pSel, pText, submode);
    Module._free(pMycall);
    Module._free(pMygrid);
    Module._free(pSel);
    Module._free(pText);
    if (!out) return null;
    let end = out;
    while (Module.HEAPU8[end] !== 0) ++end;
    const json = new TextDecoder().decode(Module.HEAPU8.subarray(out, end));
    Module._js8_free_string(out);
    try { return JSON.parse(json); } catch { return null; }
}

function encode(msg, frameType = 0, submode = 0) {
    if (msg.length !== 12) throw new Error(`msg must be 12 chars, got ${msg.length}: "${msg}"`);
    const msgPtr = Module._malloc(13);
    for (let i = 0; i < 12; i++) Module.HEAPU8[msgPtr + i] = msg.charCodeAt(i);
    Module.HEAPU8[msgPtr + 12] = 0;
    const tonesPtr = Module._malloc(79 * 4);
    const rc = Module._js8_encode(submode, frameType, msgPtr, tonesPtr);
    let tones = null;
    if (rc === 0) {
        tones = new Int32Array(79);
        for (let i = 0; i < 79; i++) tones[i] = Module.HEAP32[tonesPtr / 4 + i];
    }
    Module._free(msgPtr);
    Module._free(tonesPtr);
    return tones;
}

function decodeAll(samples, submode = 0) {
    const dec = Module._js8_decoder_new(submode);
    if (!dec) return [];
    const sPtr = Module._malloc(samples.length * 4);
    new Float32Array(Module.HEAPF32.buffer, sPtr, samples.length).set(samples);
    Module._js8_decoder_push(dec, sPtr, samples.length);
    Module._free(sPtr);
    Module._js8_decoder_run(dec);
    const out = [];
    while (true) {
        const sp = Module._js8_decoder_pop(dec);
        if (!sp) break;
        let end = sp;
        while (Module.HEAPU8[end] !== 0) ++end;
        const json = new TextDecoder().decode(Module.HEAPU8.subarray(sp, end));
        Module._js8_free_string(sp);
        try { out.push(JSON.parse(json)); } catch {}
    }
    Module._js8_decoder_free(dec);
    return out;
}

// ─── Test cases — vary in length to exercise single- and multi-frame ──

const MYCALL = "K1FM";
const MYGRID = "EM85";

// Each phrase tests a different aspect of JSC:
//   - short common word: tests the high-frequency end of the dictionary
//   - 2-word: tests inter-word separator handling
//   - longer: forces multi-frame packing (FrameData chained)
const TEST_PHRASES = [
    "HELLO",
    "HELLO WORLD",
    "WX FB ANT IS OK",
    "GOOD MORNING FROM BOSTON",
];

console.log(`\n┌─ Tier 2 — JSC free-text roundtrip ─────────────────────────────────┐`);
console.log(`│ mycall: ${MYCALL}  mygrid: ${MYGRID}                                          │`);
console.log(`└────────────────────────────────────────────────────────────────────┘`);

let allPassed = true;

for (const phrase of TEST_PHRASES) {
    console.log(`\n──── "${phrase}" ────`);

    const frames = pack(MYCALL, MYGRID, "", phrase);
    if (!frames || frames.length === 0) {
        console.log(`  FAIL — pack returned 0 frames (JSC not wired?)`);
        allPassed = false;
        continue;
    }
    for (const f of frames) {
        console.log(`  pack: type=${f.type}  frame="${f.frame}"`);
    }

    // Decode each frame's audio separately — this mirrors the SPA which
    // captures one slot of audio at a time and runs decode at each slot
    // boundary. Feeding all N frames' audio to one decode pass only finds
    // the first slot's frame (decoder's kszA scan window = one slot).
    const allDecoded = [];
    let encodeOk = true;
    for (const f of frames) {
        const tones = encode(f.frame, f.type, 0);
        if (!tones) {
            console.log(`  encode FAILED for "${f.frame}"`);
            encodeOk = false;
            break;
        }
        const audio = synthesize(tones, { submode: 0 });
        const evs = decodeAll(audio, 0).filter(e => e && e.event === "decoded");
        for (const d of evs) allDecoded.push(d);
        for (const d of evs) {
            console.log(`  decode: type=${d.frameType}  text="${d.text}"`);
        }
    }
    if (!encodeOk) { allPassed = false; continue; }

    // forceIdentify=true means unaddressed lines get "MYCALL: " prepended,
    // so the round-tripped text we expect to find is the *prepended* form.
    // Verify each whitespace-separated word of either form appears somewhere
    // in the decoded fragments.
    const allText = allDecoded.map(d => (d.text || "").toUpperCase()).join(" ");
    const wantWords = (MYCALL + ": " + phrase).toUpperCase().split(/\s+/);
    const missing = wantWords.filter(w => !allText.includes(w));
    if (allDecoded.length === 0) {
        console.log(`  FAIL — no decodes`);
        allPassed = false;
    } else if (missing.length === 0) {
        console.log(`  PASS`);
    } else {
        console.log(`  FAIL — missing words ${JSON.stringify(missing)} in "${allText}"`);
        allPassed = false;
    }
}

console.log("");
console.log(allPassed ? "TIER 2 GATE: PASS — JSC dictionary roundtrips free-text"
                      : "TIER 2 GATE: FAIL");
process.exit(allPassed ? 0 : 1);
