// Phase 1 Loop C: replay each captured WAV through the wasm decoder,
// diff against the JSON metadata's expected text. Reports per-file
// pass/fail and an overall hit-rate.
//
// Prereqs:
//   tools/test-js8-capture.sh  (or any pre-existing corpus)
//   resources/web-standalone/wasm/js8.{mjs,wasm}  (built)
//
// Run: node tools/test-js8-corpus.mjs [corpus-dir]
//   default: resources/js8-src/test/corpus/normal/

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

// ─── WAV parsing ───────────────────────────────────────────────────────
//
// Generic-enough RIFF/WAVE reader: scans chunks until the `data` chunk,
// extracts Int16 samples, returns Float32Array normalized to ±1.

function readWavInt16(buf) {
    const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
    if (buf.toString("ascii", 0, 4) !== "RIFF" ||
        buf.toString("ascii", 8, 12) !== "WAVE") {
        throw new Error("not a RIFF/WAVE file");
    }
    let p = 12;
    let fmt = null, dataOff = -1, dataLen = -1;
    while (p + 8 <= buf.length) {
        const id  = buf.toString("ascii", p, p + 4);
        const len = dv.getUint32(p + 4, true);
        if (id === "fmt ") {
            fmt = {
                audioFormat:   dv.getUint16(p + 8,  true),
                numChannels:   dv.getUint16(p + 10, true),
                sampleRate:    dv.getUint32(p + 12, true),
                bitsPerSample: dv.getUint16(p + 22, true),
            };
        } else if (id === "data") {
            dataOff = p + 8;
            dataLen = len;
            break;
        }
        p += 8 + len + (len & 1);
    }
    if (!fmt || dataOff < 0) throw new Error("WAV missing fmt/data chunk");
    if (fmt.audioFormat !== 1 || fmt.bitsPerSample !== 16 || fmt.numChannels !== 1) {
        throw new Error(`expected mono Int16 PCM (got fmt=${fmt.audioFormat}, ` +
                        `bps=${fmt.bitsPerSample}, ch=${fmt.numChannels})`);
    }
    const n = dataLen / 2;
    const out = new Float32Array(n);
    for (let i = 0; i < n; ++i) {
        out[i] = dv.getInt16(dataOff + i * 2, true) / 32767;
    }
    return { samples: out, rate: fmt.sampleRate };
}

// ─── Decoder helper ────────────────────────────────────────────────────

function decode(samples, submode = 0) {
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
        try {
            out.push(JSON.parse(new TextDecoder().decode(
                Module.HEAPU8.subarray(sp, end))));
        } catch {}
        Module._js8_free_string(sp);
    }
    Module._js8_decoder_free(dec);
    return out;
}

// ─── Corpus walker ─────────────────────────────────────────────────────

const corpusDir = process.argv[2] ||
    path.join(__dirname, "..", "resources", "js8-src", "test", "corpus", "normal");

if (!fs.existsSync(corpusDir)) {
    console.error(`Corpus directory not found: ${corpusDir}`);
    console.error(`Run tools/test-js8-capture.sh to populate it.`);
    process.exit(2);
}

const wavs = fs.readdirSync(corpusDir).filter(f => f.endsWith(".wav")).sort();
if (wavs.length === 0) {
    console.error(`No .wav files in ${corpusDir}`);
    process.exit(2);
}

console.log(`\nReplaying ${wavs.length} corpus files from ${corpusDir}\n`);

// Captures whose ground-truth `text` is empty are marked as
// "skip" — typically captures where the user's Enter timing missed
// the slot, leaving silence. They walk through the decoder but don't
// count for the hit rate.
let hits = 0, fails = 0, errs = 0, skips = 0;
const failures = [];

for (const fname of wavs) {
    const wavPath  = path.join(corpusDir, fname);
    const jsonPath = wavPath.replace(/\.wav$/, ".json");
    let expected = null;
    try { expected = JSON.parse(fs.readFileSync(jsonPath, "utf8")); } catch {}

    let samples;
    try { samples = readWavInt16(fs.readFileSync(wavPath)).samples; }
    catch (e) {
        console.log(`  ERR   ${fname}: ${e.message}`);
        ++errs;
        continue;
    }

    const decoded = decode(samples).filter(d => d.event === "decoded");
    const expectedText = expected ? (expected.text || "") : "";
    const expectedNoSpaces = expectedText.replace(/\s+/g, "").toUpperCase();

    if (!expectedNoSpaces) {
        // Ground truth says "no audio expected" — skip from hit-rate.
        ++skips;
        const note = expected && expected.source ? expected.source : "skipped";
        const got = decoded.length === 0 ? "NO DECODE" : decoded.map(d => d.text).join(" | ");
        console.log(`  SKIP  ${fname.padEnd(20)}  (${note})   got=${got}`);
        continue;
    }

    // Match: decoded text should contain the expected text (or vice
    // versa for shorter expectations like "K1FM:"), comparison is
    // case-insensitive and whitespace-insensitive.
    const matched = decoded.find(d => {
        const got = (d.text || "").replace(/\s+/g, "").toUpperCase();
        return got.includes(expectedNoSpaces) || expectedNoSpaces.includes(got);
    });

    if (matched) {
        ++hits;
        console.log(`  PASS  ${fname.padEnd(20)}  expect="${expectedText}"  ` +
                    `got="${matched.text}" (snr=${matched.snr})`);
    } else if (decoded.length > 0) {
        ++fails;
        failures.push({ fname, expected: expectedText, got: decoded.map(d => d.text) });
        console.log(`  FAIL  ${fname.padEnd(20)}  expect="${expectedText}"  ` +
                    `got=${JSON.stringify(decoded.map(d => d.text))}`);
    } else {
        ++fails;
        failures.push({ fname, expected: expectedText, got: [] });
        console.log(`  FAIL  ${fname.padEnd(20)}  expect="${expectedText}"  ` +
                    `got=NO DECODE`);
    }
}

const tested = hits + fails;
const rate = tested > 0 ? hits / tested : 0;
console.log(`\n${hits}/${tested} matched (${fails} fail, ${errs} err, ${skips} skipped)`);
console.log(`Hit rate (excluding skips): ${(rate * 100).toFixed(0)}%`);
console.log(rate >= 0.8 ? "\nLOOP C: PASS (≥80%)" : "\nLOOP C: FAIL (<80%)");
process.exit(rate >= 0.8 ? 0 : 1);
