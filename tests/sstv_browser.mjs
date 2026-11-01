// SSTV end to end in a real browser: two Standalone virtual rigs
// (?virtual=1&id=1 and id=2 share the browser air bus), never a real radio.
//
// - both tabs open the SSTV panel from the page (toggleSstvBar), which puts
//   the mic level to DIGI_MOD_LEVEL and starts the decoder worker;
// - A gets a colour-bar picture, picks Robot 36 and presses SEND: the over
//   is keyed as a digital one (enableMic, setPTT {digital: true}) and ends
//   with setPTT false;
// - B sees the VIS, paints the rows and files the finished image in its
//   gallery (IndexedDB on Standalone); the picture on B is checked against
//   what A sent;
// - closing the panel puts the mic level back.
//
// Needs playwright-core and a Chromium (found like comp_browser.mjs);
// `--probe` only reports whether they were found. Run through
// tests/test_sstv.py. WFWEB_SHOT_DIR=<dir> saves both tabs. Takes ~70 s
// (a Robot 36 picture is 36 s on the air).

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { execFileSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(here, '..');

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
      for (const r of revs) {
        for (const sub of ['chrome-linux64/chrome', 'chrome-linux/chrome']) {
          const p = path.join(base, r, sub);
          if (fs.existsSync(p)) { chrome = p; break; }
        }
        if (chrome) break;
      }
    }
    if (!chrome) for (const p of ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser']) if (fs.existsSync(p)) { chrome = p; break; }
  }
  return core && chrome ? { core, chrome } : null;
}

const found = findBrowser();
if (process.argv.includes('--probe')) { console.log(found ? `playwright-core ${found.core}\nchromium ${found.chrome}` : 'no browser'); process.exit(found ? 0 : 3); }
if (!found) { console.error('no playwright-core / Chromium found'); process.exit(3); }
const { chromium } = createRequire(import.meta.url)(found.core);

let fails = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) fails++; }

// ---- the two rigs: the Standalone bundle on two virtual rigs, or, with
// WFWEB_SSTV_URLS="https://127.0.0.1:9080 https://127.0.0.1:9090", two
// wfweb servers on the testrig bench (scripts/testrig.sh up 2): the same
// run, with the gallery on each server instead of in the browser, and a
// second tab on B's server that must see the picture arrive and go.
const SERVER_URLS = (process.env.WFWEB_SSTV_URLS || '').split(/\s+/).filter(Boolean);
const onServer = SERVER_URLS.length === 2;
const dist = onServer ? null : fs.mkdtempSync(path.join(os.tmpdir(), 'wfweb-sstv-'));
if (!onServer) execFileSync('bash', [path.join(ROOT, 'tools', 'build-static.sh'), dist], { stdio: 'ignore' });
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm', '.svg': 'image/svg+xml', '.png': 'image/png', '.json': 'application/json', '.onnx': 'application/octet-stream' };
const server = http.createServer((req, res) => {
  let p = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (p.endsWith('/')) p += 'index.html';
  const f = path.join(dist, path.normalize(p));
  if (!f.startsWith(dist) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': TYPES[path.extname(f)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
  fs.createReadStream(f).pipe(res);
});
if (!onServer) await new Promise(r => server.listen(0, '127.0.0.1', r));
const origin = onServer ? null : `http://127.0.0.1:${server.address().port}`;
const rigUrl = (id) => onServer ? SERVER_URLS[id - 1] + '/' : `${origin}/?virtual=1&id=${id}`;

const browser = await chromium.launch({
  executablePath: found.chrome, headless: true,
  args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', '--autoplay-policy=no-user-gesture-required'],
});
const shots = process.env.WFWEB_SHOT_DIR || null;
try {
  const ctx = await browser.newContext({ viewport: { width: 1200, height: 820 }, ignoreHTTPSErrors: true });
  async function rig(id) {
    const page = await ctx.newPage();
    page.on('pageerror', (e) => { console.log(`pageerror (rig ${id}): ` + e.message); fails++; });
    page.on('console', (m) => { if (m.type() === 'error') console.log(`console.error (rig ${id}): ` + m.text()); });
    await page.goto(rigUrl(id));
    await page.evaluate(() => { localStorage.clear(); sessionStorage.clear(); });
    await page.goto(rigUrl(id));
    await page.click('#startBtn');
    await page.waitForFunction(() => window.currentFreq > 0 && window.audioEnabled === true, null, { timeout: 30000 });
    await page.evaluate(() => {
      window.__cmds = [];
      const orig = window.send;
      window.send = function (obj) { window.__cmds.push(JSON.parse(JSON.stringify(obj))); return orig.apply(this, arguments); };
      const sl = document.getElementById('micGainSlider');
      window.__modBefore = sl ? parseInt(sl.value, 10) : -1;
    });
    await page.evaluate(() => { if (window.currentMode !== 'USB') window.setMode('USB'); });
    await page.waitForFunction(() => window.currentMode === 'USB', null, { timeout: 5000 });
    return page;
  }
  const A = await rig(1), B = await rig(2);
  const cmds = (page, name) => page.evaluate((n) => window.__cmds.filter(c => c.cmd === n), name);
  // The gallery is behind the GALLERY button; a thumbnail can only be clicked there.
  const openGallery = (page) => page.evaluate(() => { if (document.getElementById('sstvBar').dataset.pane !== 'gallery') document.getElementById('sstvGalleryBtn').click(); });
  // B keeps what its decoder was fed: a wrong picture is dumped with the
  // screenshots as Int16 48 kHz PCM for a post-mortem (sstv_codec_sim.mjs
  // can replay it).
  await B.evaluate(() => {
    window.__rxChunks = [];
    const orig = window.SstvPanel.feedRx;
    window.SstvPanel.feedRx = function (x) { window.__rxChunks.push(x.slice()); return orig.apply(this, arguments); };
  });

  for (const [page, name] of [[A, 'A'], [B, 'B']]) {
    await page.evaluate(() => window.toggleSstvBar());
    check(await page.evaluate(() => document.body.classList.contains('sstv-open') && window.SstvPanel.visible()), `${name}: the SSTV panel opens from the page`);
    await page.waitForFunction(() => window.SstvPanel.state.modes.length === 18, null, { timeout: 20000 });
    check(await page.evaluate(() => document.querySelectorAll('#sstvTxModeSel option').length === 18 && document.querySelectorAll('#sstvRxModeSel option').length === 19),
          `${name}: the worker reports 18 modes and the selects list them`);
    const lvl = await page.evaluate(() => parseInt(document.getElementById('micGainSlider').value, 10));
    check(lvl === await page.evaluate(() => window.DIGI_MOD_LEVEL), `${name}: the mic level is at DIGI_MOD_LEVEL while the panel is open`);
  }
  check(await A.evaluate((k) => window.SstvPanel.state.galleryKind === k, onServer ? 'server' : 'local'),
        onServer ? 'the server build keeps its gallery on the server' : 'Standalone keeps its gallery in the browser (IndexedDB)');
  // A second browser on B's server: the gallery is shared across devices.
  let B2 = null;
  if (onServer) {
    B2 = await ctx.newPage();
    B2.on('pageerror', (e) => { console.log('pageerror (B2): ' + e.message); fails++; });
    await B2.goto(rigUrl(2));
    await B2.click('#startBtn');
    await B2.waitForFunction(() => window.currentFreq > 0, null, { timeout: 30000 });
    await B2.evaluate(() => window.toggleSstvBar());
    await B2.waitForFunction(() => window.SstvPanel.state.modes.length === 18, null, { timeout: 20000 });
    // B2 hears the same audio as B; pinned to another mode it ignores the
    // Robot 36 VIS, so only B files the picture and B2 just watches.
    await B2.evaluate(() => { const sel = document.getElementById('sstvRxModeSel'); sel.value = 'PD-290'; sel.dispatchEvent(new Event('change')); });
    await B2.waitForFunction(() => window.SstvPanel.state.galleryKind === 'server', null, { timeout: 5000 });
    // The count is only meaningful once the list has come back from the server.
    await B2.waitForFunction(() => /on the server/.test(document.getElementById('sstvGalleryMeta').textContent), null, { timeout: 10000 });
    B2.before = await B2.evaluate(() => window.SstvPanel.state.gallery.length);
    check(true, `B2: watching the gallery (${B2.before} pictures already there)`);
  }
  check(await A.evaluate(() => document.querySelectorAll('#sstvBandSel option').length > 1), 'the BAND select lists the SSTV frequencies the rig can tune');
  await A.waitForTimeout(1500);
  check(await A.evaluate(() => window.SstvPanel.state.spec !== null), 'A: the tuning strip receives spectrum frames');

  // A: colour bars, Robot 36, SEND.
  await A.evaluate(async () => {
    const cv = document.createElement('canvas'); cv.width = 320; cv.height = 240;
    const g = cv.getContext('2d');
    const cols = ['#fff', '#ff0', '#0ff', '#0f0', '#f0f', '#f00', '#00f', '#000'];
    cols.forEach((c, i) => { g.fillStyle = c; g.fillRect(i * 40, 0, 40, 160); });
    const gr = g.createLinearGradient(0, 0, 320, 0); gr.addColorStop(0, '#000'); gr.addColorStop(1, '#fff');
    g.fillStyle = gr; g.fillRect(0, 160, 320, 80);
    const blob = await new Promise(r => cv.toBlob(r, 'image/png'));
    window.SstvPanel.setTxImage(blob, 'bars.png');
    const sel = document.getElementById('sstvTxModeSel');
    sel.value = 'Robot 36'; sel.dispatchEvent(new Event('change'));
  });
  await A.waitForFunction(() => window.SstvPanel.state.txImage && document.getElementById('sstvTxCanvas').width === 320 && document.getElementById('sstvTxCanvas').height === 240, null, { timeout: 5000 });
  check(true, 'A: the picture is fitted to Robot 36 (320x240)');
  check(await A.evaluate(() => { const cv = document.getElementById('sstvTxCanvas'), fr = cv.parentNode; return cv.clientWidth > 400 && cv.clientWidth === fr.clientWidth && cv.clientHeight === fr.clientHeight; }),
        'A: the TX picture fills its card');
  // A picture picked for TX goes to the gallery's LOADED section.
  await A.waitForFunction(() => window.SstvPanel.state.gallery.some(g => g.kind === 'tx' && g.name === 'bars.png'), null, { timeout: 10000 });
  const loaded = await A.evaluate(() => window.SstvPanel.state.gallery.find(g => g.kind === 'tx'));
  check(loaded.w === 320 && loaded.h === 240 && loaded.complete === true, `A: the loaded picture is in the gallery as kind tx (${JSON.stringify(loaded)})`);
  await A.click('#sstvGalleryBtn');
  check(await A.evaluate(() => document.getElementById('sstvBar').dataset.pane === 'gallery'
        && getComputedStyle(document.querySelector('.sstv-card-gallery')).display !== 'none'
        && getComputedStyle(document.querySelector('.sstv-card-tx')).display === 'none'
        && Array.from(document.querySelectorAll('.sstv-gallery-sect')).map(e => e.textContent.split(' ')[0]).join(',') === 'RECEIVED,LOADED'
        && document.querySelector('.sstv-thumb.loaded') !== null),
        'A: GALLERY shows the gallery alone, RECEIVED and LOADED apart');
  await A.click('#sstvGalleryBtn');
  check(await A.evaluate(() => getComputedStyle(document.querySelector('.sstv-card-tx')).display !== 'none'), 'A: GALLERY again brings the pictures back');
  await A.click('#sstvSendBtn');
  // What went out: SEND renders the picture once more with the overlay text
  // (the station callsign, which the server build knows only after connect).
  const sent = await A.evaluate(() => {
    const cv = document.getElementById('sstvTxCanvas');
    return Array.from(cv.getContext('2d').getImageData(0, 0, 320, 240).data);
  });
  await A.waitForFunction(() => window.__cmds.some(c => c.cmd === 'setPTT' && c.value === true), null, { timeout: 20000 });
  const keys = (await cmds(A, 'setPTT')).filter(c => c.value === true);
  check(keys.length === 1 && keys[0].digital === true, 'A: SEND keys the rig once, as a digital over');
  check((await cmds(A, 'enableMic')).some(c => c.value === true), 'A: the over is bracketed with enableMic');
  check(await A.evaluate(() => window.SstvPanel.state.txBusy && window.digiTxActive === true), 'A: TX is marked busy and the ALC loop armed');
  await A.waitForTimeout(3000);
  check(await A.evaluate(() => { const sw = document.getElementById('sstvTxSweep'), cv = document.getElementById('sstvTxCanvas');
        const p = parseFloat(sw.style.getPropertyValue('--p')); return getComputedStyle(sw).display === 'block' && p > 0 && p < 0.5 && Math.abs(sw.clientWidth - cv.clientWidth) <= 2; }),
        'A: the sweep crosses the TX picture while it goes out');

  // B: the VIS, the rows, the gallery.
  await B.waitForFunction(() => window.SstvPanel.state.rx !== null, null, { timeout: 30000 });
  const rxMode = await B.evaluate(() => window.SstvPanel.state.rx.name);
  check(rxMode === 'Robot 36', `B: the VIS says Robot 36 (got ${rxMode})`);
  await B.waitForFunction(() => window.SstvPanel.state.rx === null && /saved/.test(window.SstvPanel.state.status), null, { timeout: 90000 });
  check(true, `B: the picture ended and was filed (${await B.evaluate(() => window.SstvPanel.state.status)})`);
  await A.waitForFunction(() => !window.SstvPanel.state.txBusy, null, { timeout: 20000 });
  check((await cmds(A, 'setPTT')).some(c => c.value === false), 'A: the over ends with setPTT false');
  check(await A.evaluate(() => window.digiTxActive === false), 'A: the ALC flag is cleared after the over');
  const got = await B.evaluate(() => {
    const cv = document.getElementById('sstvRxCanvas');
    return { w: cv.width, h: cv.height, data: Array.from(cv.getContext('2d').getImageData(0, 0, cv.width, cv.height).data) };
  });
  check(got.w === 320 && got.h === 240, `B: the RX canvas is 320x240 (got ${got.w}x${got.h})`);
  let err = 0, n = 0;
  for (let i = 0; i < got.data.length; i += 4) { for (let c = 0; c < 3; c++) err += Math.abs(got.data[i + c] - sent[i + c]); n += 3; }
  const mae = err / n;
  check(mae < 25, `B: the received picture matches what A sent (mean abs error ${mae.toFixed(1)})`);
  if (mae >= 25 && shots) {
    const b64 = await B.evaluate(() => {
      let total = 0; for (const c of window.__rxChunks) total += c.length;
      const all = new Int16Array(total); let p = 0; for (const c of window.__rxChunks) { all.set(c, p); p += c.length; }
      const u8 = new Uint8Array(all.buffer); let str = '';
      for (let i = 0; i < u8.length; i += 0x8000) str += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
      return btoa(str);
    });
    fs.writeFileSync(path.join(shots, 'sstv-B-rx-48k.pcm'), Buffer.from(b64, 'base64'));
    console.log('wrote ' + path.join(shots, 'sstv-B-rx-48k.pcm'));
  }
  await B.waitForFunction(() => window.SstvPanel.state.gallery.length >= 1 && /saved/.test(window.SstvPanel.state.status), null, { timeout: 10000 });
  const item = await B.evaluate(() => window.SstvPanel.state.gallery[0]);
  check(item && item.mode === 'Robot 36' && item.complete === true && item.w === 320 && item.h === 240, `B: the gallery entry carries the metadata (${JSON.stringify(item)})`);
  check(await B.evaluate(() => document.querySelectorAll('#sstvGallery .sstv-thumb').length >= 1), 'B: the gallery shows the thumbnail');
  if (B2) {
    await B2.waitForFunction((n) => window.SstvPanel.state.gallery.length === n + 1, B2.before, { timeout: 10000 });
    check(await B2.evaluate((id) => window.SstvPanel.state.gallery[0].id === id, item.id), 'B2: the other browser sees the picture arrive (sstvGallery broadcast)');
    await openGallery(B2);                        // the thumbnails load lazily, only once the gallery is on screen
    await B2.waitForFunction(() => { const i = document.querySelector('#sstvGallery .sstv-thumb img'); return i && i.complete && i.naturalWidth === 320; }, null, { timeout: 10000 });
    check(true, 'B2: the thumbnail loads from the server (320 px wide)');
  }
  if (shots) {
    await A.screenshot({ path: path.join(shots, 'sstv-A.png') });
    await B.screenshot({ path: path.join(shots, 'sstv-B.png') });
  }
  // The viewer: TO TX loads the received picture to send it back, PIP puts
  // it in a corner of the TX picture.
  // (On Standalone both tabs share one origin, so A's loaded picture is in
  // B's gallery too: the count of loaded pictures is what must not move.)
  const loadedBefore = await B.evaluate(() => window.SstvPanel.state.gallery.filter(g => g.kind === 'tx').length);
  await openGallery(B);
  await B.click('#sstvGallery .sstv-thumb');
  await B.waitForFunction(() => document.getElementById('sstvViewer').style.display !== 'none');
  await B.click('#sstvViewerToTx');
  await B.waitForFunction(() => window.SstvPanel.state.txImage && document.getElementById('sstvViewer').style.display === 'none', null, { timeout: 10000 });
  await B.waitForTimeout(1000);
  check(await B.evaluate((n) => window.SstvPanel.state.txName.indexOf('Robot 36') === 0 && window.SstvPanel.state.gallery.filter(g => g.kind === 'tx').length === n, loadedBefore),
        'B: TO TX loads the received picture to send, without filing it again');
  await openGallery(B);
  await B.click('#sstvGallery .sstv-thumb');
  await B.waitForFunction(() => document.getElementById('sstvViewer').style.display !== 'none');
  await B.click('#sstvViewerPip');
  await B.waitForFunction(() => window.SstvPanel.state.pip !== null, null, { timeout: 10000 });
  const pipPx = await B.evaluate(() => {
    const cv = document.getElementById('sstvTxCanvas'), g = cv.getContext('2d');
    // top-right corner by default: the white border runs along y = 3..4 at the top of the inset
    const d = g.getImageData(cv.width - 20, 4, 1, 1).data, far = g.getImageData(10, cv.height - 10, 1, 1).data;
    return { corner: window.SstvPanel.state.pipCorner, white: d[0] > 200 && d[1] > 200 && d[2] > 200, label: document.getElementById('sstvPipBtn').textContent, hidden: document.getElementById('sstvPipBtn').hidden, far: Array.from(far) };
  });
  check(pipPx.corner === 'tr' && pipPx.white && pipPx.label === 'PIP TR' && !pipPx.hidden, `B: PIP draws the picture in the top-right corner (${JSON.stringify(pipPx)})`);
  await B.click('#sstvPipBtn'); await B.click('#sstvPipBtn'); await B.click('#sstvPipBtn'); await B.click('#sstvPipBtn');
  check(await B.evaluate(() => window.SstvPanel.state.pip === null && document.getElementById('sstvPipBtn').hidden), 'B: the PIP button walks the corners and then takes it off');

  // START mid-flight: A sends again; B drops the decode after a few lines
  // and presses START under AUTO. The worker locks on three sync pulses,
  // names Robot 36 from their spacing and decodes from the lines it has
  // buffered, so the picture holds everything from the lock on, shifted up
  // by the lines gone before.
  await A.click('#sstvSendBtn');
  await B.waitForFunction(() => window.SstvPanel.state.rx !== null && window.SstvPanel.state.rx.rows > 20, null, { timeout: 30000 });
  await B.click('#sstvRxDiscardBtn');
  await B.waitForFunction(() => window.SstvPanel.state.rx === null, null, { timeout: 5000 });
  await B.click('#sstvRxStartBtn');
  await B.waitForFunction(() => /START/.test(window.SstvPanel.state.status) && document.getElementById('sstvRxMeta').textContent === 'START', null, { timeout: 5000 });
  await B.waitForFunction(() => window.SstvPanel.state.rx !== null, null, { timeout: 10000 });
  const lock = await B.evaluate(() => ({ name: window.SstvPanel.state.rx.name, via: window.SstvPanel.state.rx.via, status: window.SstvPanel.state.status, force: !!window.SstvPanel.state.rxForce }));
  check(lock.name === 'Robot 36' && lock.via === 'sync' && lock.force, `B: START under AUTO locks on the sync pulses and names Robot 36 (${lock.name} via ${lock.via}: ${lock.status})`);
  await B.waitForFunction(() => window.SstvPanel.state.rx === null && /saved/.test(window.SstvPanel.state.status), null, { timeout: 90000 });
  await A.waitForFunction(() => !window.SstvPanel.state.txBusy, null, { timeout: 20000 });
  const got2 = await B.evaluate(() => Array.from(document.getElementById('sstvRxCanvas').getContext('2d').getImageData(0, 0, 320, 240).data));
  // (Compared in the gradient two thirds of the test picture: the colour
  // bars on top are the same on every row and would hide the shift.)
  let bestShift = { shift: 0, mae: 1e9 };
  for (let shift = 0; shift < 50; shift++) {
    let e = 0, n = 0;
    for (let y = 100; y < 190; y += 2) for (let x = 0; x < 320; x += 4) {
      const g = (y * 320 + x) * 4, sIdx = ((y + shift) * 320 + x) * 4;
      for (let c = 0; c < 3; c++) { e += Math.abs(got2[g + c] - sent[sIdx + c]); n++; }
    }
    if (e / n < bestShift.mae) bestShift = { shift, mae: e / n };
  }
  check(bestShift.mae < 25 && bestShift.shift >= 10, `B: the START picture holds the lines from the lock on (shifted up ${bestShift.shift} rows, mean abs error ${bestShift.mae.toFixed(1)})`);
  check(await B.evaluate(() => window.SstvPanel.state.rxForce === null && document.getElementById('sstvRxMeta').textContent === 'AUTO'), 'B: after that picture the decoder is back on AUTO');
  await B.waitForFunction((id) => window.SstvPanel.state.gallery.length >= 1 && window.SstvPanel.state.gallery[0].id !== id, item.id, { timeout: 10000 });
  const startItem = await B.evaluate(() => window.SstvPanel.state.gallery[0]);
  check(startItem.mode === 'Robot 36' && startItem.kind === 'rx', `B: the START picture is filed as received (${JSON.stringify(startItem)})`);
  if (B2) {
    await B2.waitForFunction((n) => window.SstvPanel.state.gallery.length === n + 2, B2.before, { timeout: 10000 });
    check(true, 'B2: the other browser sees the second picture arrive');
  }

  // Delete from the gallery: two presses, no confirm() dialog.
  await B.evaluate(() => { window.confirm = () => { throw new Error('confirm() must not be used'); }; });
  await openGallery(B);
  await B.click('#sstvGallery .sstv-thumb');
  await B.waitForFunction(() => document.getElementById('sstvViewer').style.display !== 'none');
  await B.click('#sstvViewerDelete');
  await B.waitForTimeout(300);
  check(await B.evaluate((id) => document.getElementById('sstvViewerDelete').textContent === 'SURE?' && window.SstvPanel.state.gallery.some(g => g.id === id), startItem.id),
        'B: the first DELETE press only arms the button');
  await B.click('#sstvViewerDelete');
  await B.waitForFunction((id) => !window.SstvPanel.state.gallery.some(g => g.id === id), startItem.id, { timeout: 10000 });
  check(true, 'B: the second press removes the image from the gallery');
  if (B2) {
    await B2.waitForFunction((n) => window.SstvPanel.state.gallery.length === n + 1, B2.before, { timeout: 10000 });
    check(true, 'B2: the other browser sees the picture go');
  }

  // Close: the mic level goes back.
  for (const [page, name] of [[A, 'A'], [B, 'B']]) {
    await page.evaluate(() => window.toggleSstvBar());
    const back = await page.evaluate(() => parseInt(document.getElementById('micGainSlider').value, 10) === window.__modBefore && !document.body.classList.contains('sstv-open'));
    check(back, `${name}: closing the panel restores the mic level`);
  }
} finally {
  await browser.close();
  if (!onServer) { server.close(); fs.rmSync(dist, { recursive: true, force: true }); }
}
console.log(fails ? `${fails} check(s) FAILED` : 'all checks passed');
process.exit(fails ? 1 : 0);
