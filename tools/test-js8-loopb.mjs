// Phase 1 Loop B — encoder verified by an external JS8Call receiver.
//
// Synthesizes a JS8 Normal frame from our wasm encoder, streams it
// through the testrig PulseAudio bridge into JS8Call's input device,
// then watches JS8Call's ALL.TXT for the corresponding decode line.
//
// Prereqs:
//   - testrig up (./scripts/testrig.sh up 0 1 --broadcast)
//   - JS8Call running, configured against external slot A:
//       Output (mic) = virtualrig-A-output      (we don't use this here)
//       Input  (rx)  = virtualrig-A-input       (we feed via the bridge)
//     mode = JS8 Normal
//
// We write to virtualrig-A-rxbus (the sink whose .monitor is remapped
// to virtualrig-A-input). Anything we play to that sink reaches JS8Call's
// input. Virtualrig itself constantly writes silence to that sink to keep
// the path alive; PA mixes our tone on top — net audible content = our tone.

import fs from "fs";
import path from "path";
import { spawn } from "child_process";
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

const ALLTXT = path.join(process.env.HOME, ".local/share/JS8Call/ALL.TXT");
if (!fs.existsSync(ALLTXT)) {
    console.error(`ALL.TXT not found at ${ALLTXT}`);
    console.error("Is JS8Call running and has it transmitted/received at least once?");
    process.exit(2);
}

const SAMPLE_RATE = 12000;

function encode(msg, frameType = 0) {
    if (msg.length !== 12) throw new Error("must be 12 chars");
    const msgPtr = Module._malloc(13);
    for (let i = 0; i < 12; ++i) Module.HEAPU8[msgPtr + i] = msg.charCodeAt(i);
    Module.HEAPU8[msgPtr + 12] = 0;
    const tonesPtr = Module._malloc(79 * 4);
    const rc = Module._js8_encode(0, frameType, msgPtr, tonesPtr);  // submode=Normal
    let out = null;
    if (rc === 0) {
        out = new Int32Array(79);
        for (let i = 0; i < 79; ++i) out[i] = Module.HEAP32[tonesPtr / 4 + i];
    }
    Module._free(msgPtr); Module._free(tonesPtr);
    return out;
}

function writeWavFile(filePath, samples, rate = SAMPLE_RATE) {
    const dataBytes = samples.length * 2;
    const buf = Buffer.alloc(44 + dataBytes);
    buf.write("RIFF", 0);
    buf.writeUInt32LE(36 + dataBytes, 4);
    buf.write("WAVE", 8);
    buf.write("fmt ", 12);
    buf.writeUInt32LE(16, 16);
    buf.writeUInt16LE(1, 20);          // PCM
    buf.writeUInt16LE(1, 22);          // mono
    buf.writeUInt32LE(rate, 24);
    buf.writeUInt32LE(rate * 2, 28);
    buf.writeUInt16LE(2, 32);
    buf.writeUInt16LE(16, 34);
    buf.write("data", 36);
    buf.writeUInt32LE(dataBytes, 40);
    for (let i = 0; i < samples.length; ++i) {
        const v = Math.max(-1, Math.min(1, samples[i]));
        buf.writeInt16LE(Math.round(v * 32767), 44 + i * 2);
    }
    fs.writeFileSync(filePath, buf);
}

// ─── Slot timing ───────────────────────────────────────────────────────

function waitForNextSlot(submodeSeconds = 15) {
    return new Promise(resolve => {
        const now = Date.now();
        const slotMs = submodeSeconds * 1000;
        // Wait for the next slot boundary. JS8 slots align to UTC, so
        // the next slot start is the next multiple of slotMs UTC.
        const nextSlot = Math.ceil(now / slotMs) * slotMs;
        const wait = nextSlot - now;
        const t = new Date(nextSlot).toISOString().substring(11, 19);
        console.log(`  waiting ${(wait/1000).toFixed(1)}s for slot start (UTC ${t})`);
        setTimeout(resolve, wait);
    });
}

// ─── Play WAV via pacat ────────────────────────────────────────────────

function playToSink(wavPath, sink) {
    return new Promise((resolve, reject) => {
        const p = spawn("pacat",
            ["--device=" + sink, "--rate=12000", "--channels=1",
             "--format=s16le", "--no-remix", "--no-remap", wavPath],
            { stdio: ["ignore", "ignore", "pipe"] });
        let stderr = "";
        p.stderr.on("data", d => { stderr += d; });
        p.on("close", code => {
            if (code !== 0) reject(new Error(`pacat exit ${code}: ${stderr}`));
            else resolve();
        });
    });
}

// ─── ALL.TXT scrape ────────────────────────────────────────────────────

function readNewLines(filePath, fromOffset) {
    const stat = fs.statSync(filePath);
    if (stat.size <= fromOffset) return { lines: [], newOffset: stat.size };
    const fd = fs.openSync(filePath, "r");
    const buf = Buffer.alloc(stat.size - fromOffset);
    fs.readSync(fd, buf, 0, buf.length, fromOffset);
    fs.closeSync(fd);
    return { lines: buf.toString("utf8").split("\n").filter(Boolean), newOffset: stat.size };
}

// ─── Test ─────────────────────────────────────────────────────────────

const message = "HELLOWK1FMab";   // 12 chars, JS8 alphabet
console.log(`Loop B — pipe encoder output through JS8Call for "${message}"`);
console.log(`  ALL.TXT: ${ALLTXT}`);
console.log(`  bridge:  pacat → virtualrig-A-rxbus → JS8Call's "virtualrig-A-input"`);
console.log();

const tones = encode(message);
if (!tones) { console.error("encode FAILED"); process.exit(1); }
const audio = synthesize(tones);

const tmpWav = "/tmp/js8-loopb.wav";
writeWavFile(tmpWav, audio);
console.log(`  synthesized ${(audio.length / SAMPLE_RATE).toFixed(2)}s @ 1500 Hz, saved ${tmpWav}`);

// Snapshot ALL.TXT size before the test
const baselineOffset = fs.statSync(ALLTXT).size;
console.log(`  ALL.TXT baseline offset: ${baselineOffset}`);

await waitForNextSlot(15);
const txStart = new Date();
console.log(`  slot start at UTC ${txStart.toISOString().substring(11, 19)} — playing audio`);
await playToSink(tmpWav, "virtualrig-A-rxbus");
console.log("  audio finished, giving JS8Call up to 30s to decode...");

// Poll ALL.TXT for ~30s for new lines
const deadline = Date.now() + 30000;
let allLines = [];
while (Date.now() < deadline) {
    await new Promise(r => setTimeout(r, 1000));
    const { lines } = readNewLines(ALLTXT, baselineOffset);
    if (lines.length !== allLines.length) {
        console.log(`    [+${lines.length - allLines.length} new line(s) in ALL.TXT]`);
        allLines = lines;
    }
}

console.log("\n──── New ALL.TXT lines after our transmission ────");
if (allLines.length === 0) {
    console.log("  (none)");
} else {
    allLines.forEach(l => console.log(`  ${l}`));
}

// Pass criterion: a line containing the encoded message text appears
// in the new ALL.TXT entries. JS8Call's display will show structured
// text, not the raw 12-char alphabet — but for HELLOWK1FMab (which is
// a plain JS8 frame, type 0) it should appear as "HELLOWK1FMab" or
// "Mycall: HELLOWK1FMab" etc. depending on how JS8Call interprets it.
const found = allLines.find(l => l.includes(message) ||
                                  l.toUpperCase().includes(message.toUpperCase()));
if (found) {
    console.log(`\nLOOP B: PASS — JS8Call decoded "${message}":`);
    console.log(`  ${found}`);
    process.exit(0);
} else {
    console.log(`\nLOOP B: FAIL — JS8Call did not log a decode containing "${message}".`);
    console.log("Things to check:");
    console.log("  • Is JS8Call's mode = JS8 Normal?");
    console.log("  • Audio input device = virtualrig-A-input  (description: 'external-A Input...')?");
    console.log("  • Frequency display 14.078 MHz USB? (anywhere is fine — we only use audio)");
    console.log("  • RX waterfall in JS8Call shows our 1500 Hz tone?");
    process.exit(1);
}
