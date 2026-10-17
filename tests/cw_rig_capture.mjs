// Keyer timing of a REAL Icom radio, measured off the air (#141).
//
// The two-rig bench (tests/cw_bench.mjs) keys the virtual rig at exactly
// 1200 / WPM ms per dot with 1 / 3 / 7 gaps, the same arithmetic as the
// page's model, so it cannot see a real keyer's timing.  This tool keys the
// bench IC-7300 through the page's own keyer (pacing included) at a range
// of speeds, captures the radio's sidetone as it comes back in the USB RX
// audio, and compares the keyed elements with the model: start latency,
// dot / dash / gap lengths, drift and rate error per speed.
//
// Run by hand, never by pytest:
//     node tests/cw_rig_capture.mjs [--speeds 12,20,30] [--out file.json]
//
// NO RF: the radio's BK-IN must be OFF (front panel), so the keyer only
// produces the sidetone.  The tool reads Break-In (CI-V 16 47) on the raw
// serial port before starting the server and refuses to key unless it is 0;
// it reads it again through rigctld before every text, and aborts the run
// if the radio ever reports TX.  It never sets BK-IN itself.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import net from 'node:net';
import { spawn, spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(here, '..');
const argv = process.argv.slice(2);
const opt = (name, dflt) => { const i = argv.indexOf(name); return i >= 0 && argv[i + 1] !== undefined ? argv[i + 1] : dflt; };
const DEV = opt('--dev', '/dev/ttyUSB0');
const CIV_ADDR = parseInt(opt('--civ', '0x94'), 16);
const URL = opt('--url', 'https://127.0.0.1:8080/');
const RIGCTL_PORT = parseInt(opt('--rigctld', '4532'), 10);
const SPEEDS = opt('--speeds', '12,16,20,25,30,35,40,48').split(',').map(Number);
const OUT = opt('--out', path.join(os.tmpdir(), 'cw_rig_capture.json'));
const NO_START = argv.includes('--no-start');
const LOG = path.join(os.tmpdir(), 'wfweb-cw-capture.log');
const PARIS = opt('--text', 'PARIS PARIS PARIS PARIS PARIS');
const LONG = 'THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 73 DE W1AAA K';

const sleep = ms => new Promise(r => setTimeout(r, ms));
const median = a => { if (!a.length) return NaN; const s = a.slice().sort((x, y) => x - y); const m = s.length >> 1; return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2; };
const fmt = (v, d = 0) => (Number.isFinite(v) ? v.toFixed(d) : '-');
function die(msg, code = 2) { console.error('ABORT: ' + msg); process.exit(code); }

// ---------------------------------------------------------------- browser --
function findBrowser() {
  const home = os.homedir();
  let core = process.env.WFWEB_PLAYWRIGHT_CORE || null;
  if (!core) {
    const cands = [];
    for (const base of [path.join(home, '.npm', '_npx'), path.join(ROOT, 'node_modules')]) {
      if (!fs.existsSync(base)) continue;
      const dirs = base.endsWith('node_modules') ? [base] : fs.readdirSync(base).map(d => path.join(base, d, 'node_modules'));
      for (const d of dirs) {
        const p = path.join(d, 'playwright-core');
        if (fs.existsSync(path.join(p, 'package.json'))) cands.push({ p, v: JSON.parse(fs.readFileSync(path.join(p, 'package.json'))).version });
      }
    }
    cands.sort((a, b) => b.v.localeCompare(a.v, undefined, { numeric: true }));
    if (cands.length) core = cands[0].p;
  }
  let chrome = process.env.WFWEB_CHROMIUM || null;
  if (!chrome) {
    const base = path.join(home, '.cache', 'ms-playwright');
    if (fs.existsSync(base)) {
      const revs = fs.readdirSync(base).filter(d => /^chromium-\d+$/.test(d)).sort((a, b) => parseInt(b.slice(9)) - parseInt(a.slice(9)));
      for (const r of revs) { for (const sub of ['chrome-linux64/chrome', 'chrome-linux/chrome']) { const p = path.join(base, r, sub); if (fs.existsSync(p)) { chrome = p; break; } } if (chrome) break; }
    }
    if (!chrome) for (const p of ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser']) if (fs.existsSync(p)) { chrome = p; break; }
  }
  return core && chrome ? { core, chrome } : null;
}

// ------------------------------------------------- raw CI-V on the serial --
// One query on the bare port (the server is not running yet): returns the
// data bytes of the reply as hex, or null.
function civQueryRaw(cmdBytes) {
  const py = `
import os, termios, select, time, sys
dev, addr, cmd = sys.argv[1], int(sys.argv[2]), bytes.fromhex(sys.argv[3])
fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL; a[3] = 0
a[4] = termios.B115200; a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
termios.tcflush(fd, termios.TCIOFLUSH)
os.write(fd, bytes([0xFE, 0xFE, addr, 0xE0]) + cmd + b'\\xFD')
buf = b''; end = time.time() + 1.5
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try: buf += os.read(fd, 4096)
        except BlockingIOError: pass
    i = buf.find(bytes([0xFE, 0xFE, 0xE0, addr]) + cmd)
    if i >= 0:
        j = buf.find(b'\\xFD', i)
        if j > 0:
            print(buf[i + 4 + len(cmd):j].hex()); sys.exit(0)
sys.exit(1)
`;
  const r = spawnSync('python3', ['-I', '-c', py, DEV, String(CIV_ADDR), Buffer.from(cmdBytes).toString('hex')], { encoding: 'utf8' });
  return r.status === 0 ? r.stdout.trim() : null;
}

// ------------------------------------------------------------------ rigctld --
function rigctl(cmd) {
  return new Promise((resolve) => {
    const s = net.createConnection(RIGCTL_PORT, '127.0.0.1');
    let out = ''; const t = setTimeout(() => { s.destroy(); resolve(null); }, 2000);
    s.on('connect', () => s.write(cmd + '\n'));
    s.on('data', d => { out += d.toString(); if (out.includes('\n')) { clearTimeout(t); s.destroy(); resolve(out.trim().split('\n')[0]); } });
    s.on('error', () => { clearTimeout(t); resolve(null); });
  });
}
// Break-In through the running server: 0 = OFF.  An empty cache also reads
// 0, so this is the second guard, not the first (the raw read is).
async function breakInViaRigctl() {
  let last = null;
  for (let i = 0; i < 4; i++) { last = await rigctl('u SBKIN'); await sleep(400); }
  return last;
}

// ------------------------------------------------------------------ server --
let server = null;
async function startServer() {
  const lsof = spawnSync('lsof', [DEV], { encoding: 'utf8' });
  if (lsof.stdout.trim()) die(DEV + ' is in use:\n' + lsof.stdout);
  server = spawn(path.join(ROOT, 'wfweb'), ['-l', LOG, '--rigctld-port', String(RIGCTL_PORT)], { cwd: ROOT, stdio: ['pipe', 'ignore', 'ignore'] });
  for (let i = 0; i < 60; i++) {
    await sleep(500);
    try { const r = await fetch('http://127.0.0.1:8081/api/v1/radio/info'); if (r.ok) return; } catch (e) {}
  }
  die('server did not come up (see ' + LOG + ')');
}
async function stopServer() {
  if (!server) return;
  try { server.stdin.write('q\n'); } catch (e) {}
  const done = new Promise(r => server.on('exit', r));
  await Promise.race([done, sleep(10000)]);
  if (server.exitCode === null) server.kill();
}

// -------------------------------------------------------------------- page --
async function bringUp(page) {
  await page.goto(URL);
  await page.evaluate(() => { localStorage.clear(); sessionStorage.clear(); });
  await page.goto(URL);
  await page.click('#startBtn');
  await page.waitForFunction(() => window.currentFreq > 0, null, { timeout: 20000 });
  await page.waitForFunction(() => window.audioEnabled === true, null, { timeout: 20000 });
  await page.waitForFunction(() => window.currentMode && window.currentMode !== '---', null, { timeout: 10000 }).catch(() => {});
}
// Tone detector on the RX PCM as it arrives: Goertzel at the rig's CW pitch
// over a 4 ms window hopping 1 ms, on a sample-count timeline (immune to
// arrival jitter; band noise under the sidetone is rejected by the filter).
async function tapRx(page) {
  await page.evaluate(() => {
    const fs = window.audioSampleRate || 48000;
    const hop = Math.round(fs / 1000), N = hop * 4;
    const w = 2 * Math.PI * (window.cwPitchHz || 600) / fs, coeff = 2 * Math.cos(w);
    window.__cap = { fs, hop, N, t0: 0, mag: [], pend: [] };
    const orig = window.handleAudioData;
    window.handleAudioData = function (buffer) {
      try {
        const cap = window.__cap;
        if (!cap.t0) cap.t0 = Date.now();
        const pcm = new Int16Array(buffer, 6);
        for (let i = 0; i < pcm.length; i++) cap.pend.push(pcm[i] / 32768);
        while (cap.pend.length >= cap.N) {
          let s0 = 0, s1 = 0, s2 = 0;
          for (let i = 0; i < cap.N; i++) { s0 = cap.pend[i] + coeff * s1 - s2; s2 = s1; s1 = s0; }
          const p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
          cap.mag.push(Math.sqrt(Math.max(0, p)) / cap.N);
          cap.pend.splice(0, cap.hop);
        }
      } catch (e) {}
      return orig.apply(this, arguments);
    };
    window.__capReset = () => { const c = window.__cap; c.t0 = 0; c.mag = []; c.pend = []; };
    window.__capGet = () => ({ t0: window.__cap.t0, mag: window.__cap.mag, hopMs: 1000 * window.__cap.hop / window.__cap.fs });
  });
}
const modelElements = page => page.evaluate(() => ({ el: window.cwKeyer.elements(0), end: window.cwKeyer.base + window.cwKeyer.nextTime, wpm: window.cwSpeed, dot: window.cwKeyer.dot() }));

// ---------------------------------------------------------------- analysis --
// Tone bursts from the magnitude trace: threshold half-way (geometric) between
// the quiet and the loud level, 20 % hysteresis, bursts < 5 ms dropped.
function bursts(cap) {
  const m = cap.mag, s = m.slice().sort((a, b) => a - b);
  const lo = s[Math.floor(s.length * 0.2)], hi = s[Math.floor(s.length * 0.95)];
  if (!(hi > 4 * lo)) return { list: [], lo, hi };
  const thr = Math.sqrt(lo * hi), on = thr * 1.2, off = thr * 0.8;
  const list = []; let cur = null;
  for (let i = 0; i < m.length; i++) {
    const t = i * cap.hopMs;
    if (!cur && m[i] > on) cur = { on: t };
    else if (cur && m[i] < off) { cur.off = t; if (cur.off - cur.on >= 5) list.push(cur); cur = null; }
  }
  if (cur) { cur.off = m.length * cap.hopMs; list.push(cur); }
  return { list, lo, hi };
}
function analyze(cap, model) {
  const b = bursts(cap);
  const rb = b.list;
  const res = { level: { quiet: b.lo, loud: b.hi }, rigBursts: rb.length, modelElements: model.el.length, modelDot: model.dot };
  if (rb.length < 3) return res;
  const dur = rb.map(x => x.off - x.on);
  // Split dots from dashes at the widest relative jump in the sorted durations.
  const sd = dur.slice().sort((a, b) => a - b);
  let cut = -1, best = 1.6;
  for (let i = 1; i < sd.length; i++) { const r = sd[i] / sd[i - 1]; if (r > best) { best = r; cut = i; } }
  const dots = cut > 0 ? sd.slice(0, cut) : sd, dashes = cut > 0 ? sd.slice(cut) : [];
  const dot = median(dots);
  res.dot = dot; res.dash = median(dashes); res.dashDots = res.dash / dot;
  const gaps = [];
  for (let i = 1; i < rb.length; i++) gaps.push(rb[i].on - rb[i - 1].off);
  res.gapIntra = median(gaps.filter(g => g < 2 * dot));
  res.gapChar = median(gaps.filter(g => g >= 2 * dot && g < 5 * dot));
  res.gapWord = median(gaps.filter(g => g >= 5 * dot));
  res.gapIntraDots = res.gapIntra / dot; res.gapCharDots = res.gapChar / dot; res.gapWordDots = res.gapWord / dot;
  // Rig vs model.  Element i of the rig against element i of the model when
  // the counts agree (else nearest model element after anchoring on the first).
  const el = model.el;
  // Match bursts to model elements: start from the first burst, then refine
  // the offset with the median residual twice, so one spurious burst (noise
  // before the text, a split element) cannot drag the whole alignment.
  let offset = rb[0].on - (el[0].on - cap.t0);
  let pairs = [];
  for (let pass = 0; pass < 3; pass++) {
    pairs = [];
    for (const b of rb) {
      let best = null, bd = Infinity;
      for (const c of el) { const d = Math.abs((c.on - cap.t0) + offset - b.on); if (d < bd) { bd = d; best = c; } }
      pairs.push({ rig: b.on, model: best.on - cap.t0, resid: b.on - ((best.on - cap.t0) + offset) });
    }
    offset += median(pairs.map(p => p.resid));
  }
  const good = pairs.filter(p => Math.abs(p.resid) < 1.5 * model.dot);
  res.spurious = pairs.length - good.length;
  const lag0 = offset;                               // includes the audio pipeline
  const rel = good.map(p => ({ rig: p.rig - good[0].rig, model: p.model - good[0].model }));
  // Least-squares slope rig = a * model: the keyer's speed relative to the model.
  let sxx = 0, sxy = 0; for (const p of rel) { sxx += p.model * p.model; sxy += p.model * p.rig; }
  res.rate = sxx ? sxy / sxx : NaN;
  res.lag0 = lag0;
  res.drift = rel[rel.length - 1].rig - rel[rel.length - 1].model;
  res.rigSpan = rb[rb.length - 1].off - rb[0].on;
  res.modelSpan = el[el.length - 1].off - el[0].on;
  res.rigWpmEff = 1200 / dot;
  return res;
}

// ---------------------------------------------------------------------- main --
(async () => {
  const found = findBrowser();
  if (!found) die('no playwright-core / Chromium found', 3);
  const { chromium } = createRequire(import.meta.url)(found.core);

  if (!NO_START) {
    // Guard 1: Break-In on the bare serial port, before anything else runs.
    const bk = civQueryRaw([0x16, 0x47]);
    if (bk === null) die('could not read Break-In (16 47) from ' + DEV);
    if (bk !== '00') die('Break-In is ' + bk + ' (1 = semi, 2 = full). Set BK-IN OFF on the radio; nothing was keyed.');
    console.log('Break-In OFF on the raw port (16 47 -> 00).');
    const spd = civQueryRaw([0x14, 0x0C]);
    console.log('radio key speed level before: ' + spd);
    await startServer();
  }

  const browser = await chromium.launch({ executablePath: found.chrome, args: ['--ignore-certificate-errors', '--autoplay-policy=no-user-gesture-required'] });
  const ctx = await browser.newContext({ ignoreHTTPSErrors: true });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  const results = { date: new Date().toISOString(), url: URL, runs: [], errors };
  const finish = async (code) => {
    try { await page.evaluate(() => window.cwKeyer && window.cwKeyer.stop()); } catch (e) {}
    try { if (results.mode0 && results.mode0 !== 'CW') await page.evaluate(m => window.setMode(m), results.mode0); } catch (e) {}
    try { if (results.wpm0) await page.evaluate(w => { window.cwSpeed = w; window.send({ cmd: 'setCWSpeed', wpm: w }); }, results.wpm0); await sleep(500); } catch (e) {}
    await browser.close().catch(() => {});
    await stopServer();
    fs.writeFileSync(OUT, JSON.stringify(results, null, 1));
    console.log('written ' + OUT);
    process.exit(code);
  };

  await bringUp(page);
  results.mode0 = await page.evaluate(() => window.currentMode);
  for (let i = 0; i < 10 && !results.wpm0; i++) { results.wpm0 = parseInt((await rigctl('l KEYSPD')) || '0', 10) || null; if (!results.wpm0) await sleep(500); }
  console.log('radio: mode ' + results.mode0 + ', keyer ' + results.wpm0 + ' WPM, pitch ' + (await page.evaluate(() => window.cwPitchHz)) + ' Hz, audio ' + (await page.evaluate(() => window.audioSampleRate)) + ' Hz');
  console.log('page speed range: ' + JSON.stringify(await page.evaluate(() => window.cwSpeedRange)));
  await page.evaluate(() => window.setMode('CW'));
  await page.waitForFunction(() => window.currentMode === 'CW', null, { timeout: 10000 });
  await page.evaluate(() => window.renderCWOverlay());
  await page.waitForFunction(() => !document.getElementById('cwBar').classList.contains('hidden'));
  await tapRx(page);
  await sleep(1000);

  // Guard 2: Break-In through the server, then a single dit while watching TX.
  const bk2 = await breakInViaRigctl();
  if (bk2 !== '0') { console.error('rigctld u SBKIN = ' + bk2); await finish(2); }
  await page.evaluate(() => { window.__capReset(); window.__txSeen = false; const o = window.updateTxStatus; window.updateTxStatus = function (tx) { if (tx) window.__txSeen = true; return o.apply(this, arguments); }; });
  await page.evaluate(() => window.sendCWText('E'));
  for (let i = 0; i < 15; i++) { await sleep(100); if (await page.evaluate(() => window.__txSeen || window.isTransmitting)) { console.error('RADIO REPORTS TX — BK-IN is not OFF. Stopping.'); await finish(4); } }
  const probeCap = await page.evaluate(() => window.__capGet());
  results.probeTrace = probeCap.mag.map(v => +v.toExponential(2));
  const probe = bursts(probeCap);
  if (probe.list.length < 1) { console.error('no sidetone in the RX audio (quiet ' + probe.lo.toExponential(2) + ', loud ' + probe.hi.toExponential(2) + '): on the IC-7300 MENU > SET > Connectors > ACC/USB AF Beep/Speech... Output must be ON'); await finish(5); }
  console.log('probe dit heard: ' + fmt(probe.list[0].off - probe.list[0].on) + ' ms (quiet ' + probe.lo.toExponential(2) + ', loud ' + probe.hi.toExponential(2) + ')');

  async function run(wpm, text, label) {
    if ((await rigctl('u SBKIN')) !== '0') { console.error('Break-In changed mid-run'); await finish(2); }
    await page.evaluate(() => window.cwKeyer.stop());
    await page.evaluate(w => { window.cwSpeed = w; window.send({ cmd: 'setCWSpeed', wpm: w }); }, wpm);
    await sleep(800);
    await rigctl('l KEYSPD'); await sleep(400);
    const rigWpm = await rigctl('l KEYSPD');
    await page.evaluate(() => window.__capReset());
    await sleep(300);
    await page.evaluate(t => window.sendCWText(t), text);
    const m = await modelElements(page);
    await sleep(Math.max(0, m.end - Date.now()) + 2500);
    if (await page.evaluate(() => window.__txSeen)) { console.error('RADIO REPORTS TX. Stopping.'); await finish(4); }
    const cap = await page.evaluate(() => window.__capGet());
    const r = analyze(cap, m);
    r.trace = cap.mag.map(v => +v.toExponential(2)); r.t0 = cap.t0; r.model = m;
    r.wpm = wpm; r.rigWpm = rigWpm; r.text = text; r.label = label;
    results.runs.push(r);
    console.log(`${label.padEnd(6)} ${String(wpm).padStart(2)} WPM (rig says ${rigWpm}): dot ${fmt(r.dot, 1)} ms (model ${fmt(r.modelDot, 1)}) = ${fmt(r.rigWpmEff, 2)} WPM eff, dash ${fmt(r.dashDots, 2)} dots, gaps ${fmt(r.gapIntraDots, 2)} / ${fmt(r.gapCharDots, 2)} / ${fmt(r.gapWordDots, 2)} dots, elements ${r.rigBursts}/${r.modelElements}${r.spurious ? ' (' + r.spurious + ' spurious)' : ''}, lag0 ${fmt(r.lag0)} ms, drift ${fmt(r.drift)} ms over ${fmt(r.modelSpan / 1000, 1)} s, rate ${fmt(r.rate, 4)}`);
    return r;
  }

  for (const wpm of SPEEDS) await run(wpm, PARIS, 'PARIS');
  if (!argv.includes('--no-long')) for (const wpm of [20, 30]) await run(wpm, LONG, 'LONG');

  console.log('\n| WPM | text | rig dot ms | model dot ms | dash | intra | char | word | lag0 ms | drift ms | span s | rate |');
  console.log('|---|---|---|---|---|---|---|---|---|---|---|---|');
  for (const r of results.runs) console.log(`| ${r.wpm} | ${r.label} | ${fmt(r.dot, 1)} | ${fmt(r.modelDot, 1)} | ${fmt(r.dashDots, 2)} | ${fmt(r.gapIntraDots, 2)} | ${fmt(r.gapCharDots, 2)} | ${fmt(r.gapWordDots, 2)} | ${fmt(r.lag0)} | ${fmt(r.drift)} | ${fmt(r.modelSpan / 1000, 1)} | ${fmt(r.rate, 4)} |`);
  if (errors.length) console.log('page errors: ' + JSON.stringify(errors));
  await finish(0);
})().catch(async e => { console.error(e); await stopServer(); process.exit(2); });
