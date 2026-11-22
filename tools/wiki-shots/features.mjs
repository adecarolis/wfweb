// Pictures for the wiki's feature pages, taken on the two-rig virtual bench.
//
//   scripts/testrig.sh up 2
//   node tools/wiki-shots/features.mjs <out dir> [shot ...]
//
// Rig A (https://127.0.0.1:9080) transmits, rig B (9090) receives, so every
// decode in the pictures is a real one off the virtual air.  Shots are
// named after the wiki page they go in; with no names, all of them run.
import fs from 'node:fs'; import os from 'node:os'; import path from 'node:path'; import { createRequire } from 'node:module';

const [outArg, ...only] = process.argv.slice(2);
if (!outArg) { console.error('usage: features.mjs <out dir> [shot ...]'); process.exit(2); }
const out = path.resolve(outArg); fs.mkdirSync(out, { recursive: true });
const npx = path.join(os.homedir(), '.npm', '_npx'); let core = null;
for (const d of fs.readdirSync(npx)) { const p = path.join(npx, d, 'node_modules', 'playwright-core'); if (fs.existsSync(p)) core = p; }
const { chromium } = createRequire(import.meta.url)(core);
const base = path.join(os.homedir(), '.cache', 'ms-playwright');
const rev = fs.existsSync(base) ? fs.readdirSync(base).filter(d => /^chromium-\d+$/.test(d)).sort().pop() : null;
const exe = (rev ? ['chrome-linux64/chrome', 'chrome-linux/chrome'].map(s => path.join(base, rev, s)) : [])
    .concat(['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser']).find(fs.existsSync);
const br = await chromium.launch({ executablePath: exe, args: ['--autoplay-policy=no-user-gesture-required', '--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream'] });

const REPO = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..', '..');
const URL_A = 'https://127.0.0.1:9080', URL_B = 'https://127.0.0.1:9090', URL_C = 'https://127.0.0.1:9100';
const CALL_A = 'W1ABC', GRID_A = 'FN42', CALL_B = 'K2DEF', GRID_B = 'FN20', CALL_C = 'N3GHI', GRID_C = 'EM79';
const W = 1280, H = 800;

const png = n => path.join(out, n + '.png');
// Per-rig noise floor on the virtual rig (rig 0 = A, 1 = B, 2 = C).  Only the
// rig in the picture gets one: decoding a slot of noise is slow in a headless
// browser, and the rigs that only transmit must keep to their slots.
const noise = (rig, rms) => fetch('http://127.0.0.1:5900/api/set', { method: 'POST', headers: { 'Content-Type': 'application/json' },
                                  body: JSON.stringify({ type: 'noise', rig, rms }) });
const sleep = ms => new Promise(r => setTimeout(r, ms));
const log = s => console.log(s);

// A page on a rig: fresh storage, START pressed, audio flowing, back on 20 m USB.
async function rig(url, { w = W, h = H, dpr = 2, call, grid, ls = {}, mobile = false } = {}) {
    const ctx = await br.newContext({ ignoreHTTPSErrors: true, viewport: { width: w, height: h }, deviceScaleFactor: dpr,
                                      isMobile: mobile, hasTouch: mobile, permissions: ['microphone'] });
    const p = await ctx.newPage();
    p.wfUrl = url; open_.push(p);
    p.on('pageerror', e => log('  PAGEERROR ' + url + ' ' + e.message));
    if (process.env.SHOTS_CONSOLE) p.on('console', m => fs.appendFileSync(path.join(out, 'console-' + url.slice(-4) + '.log'), new Date().toISOString().slice(11, 23) + ' ' + m.text() + '\n'));
    await p.goto(url);
    const store = Object.assign({}, call ? { digiMyCall: call } : {}, grid ? { digiGrid: grid } : {}, ls);
    await p.evaluate(kv => { localStorage.clear(); sessionStorage.clear(); for (const k in kv) localStorage.setItem(k, kv[k]); }, store);
    if (call || grid) await p.evaluate(async b => { await fetch('/api/v1/station', { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(b) }); },
                                       Object.assign(call ? { callsign: call } : {}, grid ? { grid } : {}));
    await p.goto(url);
    await p.click('#startBtn');
    await p.waitForFunction(() => window.currentFreq > 0 && window.audioEnabled === true, null, { timeout: 30000 });
    // FreeDV is the server's state and outlives a page: a shot starts without it.
    await p.evaluate(() => { if (window.freedvEnabled) send({ cmd: 'setFreeDV', enabled: false }); });
    await until(p, () => !window.freedvEnabled, null, 8000);
    await p.evaluate(() => { send({ cmd: 'setFrequency', value: 14074000 }); if (window.currentMode !== 'USB') setMode('USB'); });
    await p.waitForFunction(() => window.currentMode === 'USB' && Math.abs(window.currentFreq - 14074000) < 10, null, { timeout: 8000 });
    if (call) await p.evaluate(c => window.App && App.callsign && App.callsign.set(c), call);
    await sleep(800);
    return p;
}
const shot = async (p, name, opts = {}) => { await p.screenshot({ path: png(name), ...opts }); log('  ' + name); };
// A rig that only transmits: small and cheap to draw.
const helper = (url, o = {}) => rig(url, Object.assign({ w: 900, h: 640, dpr: 1 }, o));
// The union of the given elements, padded.
async function crop(p, name, sels, pad = 0) {
    const boxes = [];
    for (const s of [].concat(sels)) { const b = await p.locator(s).first().boundingBox(); if (b) boxes.push(b); }
    if (!boxes.length) { log('  ' + name + ': nothing to crop (' + sels + ')'); return; }
    const vp = p.viewportSize();
    const x0 = Math.max(0, Math.min(...boxes.map(b => b.x)) - pad), y0 = Math.max(0, Math.min(...boxes.map(b => b.y)) - pad);
    const x1 = Math.min(vp.width, Math.max(...boxes.map(b => b.x + b.width)) + pad), y1 = Math.min(vp.height, Math.max(...boxes.map(b => b.y + b.height)) + pad);
    await shot(p, name, { clip: { x: x0, y: y0, width: x1 - x0, height: y1 - y0 } });
}
const open_ = [];
const close = async (...ps) => { for (const p of ps) { open_.splice(open_.indexOf(p), 1); await p.context().close(); } };

// The RADE test recording (resources/radae_nopy/wav/peter.wav) at 48 kHz,
// as base64 Int16 for the page.
function speech() {
    const wav = fs.readFileSync(path.join(REPO, 'resources/radae_nopy/wav/peter.wav'));
    const rate = wav.readUInt32LE(24), dataAt = wav.indexOf('data') + 8;
    const src = new Int16Array(wav.buffer.slice(wav.byteOffset + dataAt, wav.byteOffset + wav.length - ((wav.length - dataAt) % 2)));
    let peak = 1; for (const v of src) peak = Math.max(peak, Math.abs(v));
    const n = Math.floor(src.length * 48000 / rate), pcm = new Int16Array(n);
    for (let i = 0; i < n; i++) { const x = i * rate / 48000, k = Math.floor(x), f = x - k; pcm[i] = ((src[k] || 0) * (1 - f) + (src[k + 1] || 0) * f) / peak * 0.8 * 32767; }
    return Buffer.from(pcm.buffer).toString('base64');
}
// Key the rig on page p and stream the recording as its mic for `seconds`
// (looped), the way the page's own mic sends 0x03 frames.
const talk = (p, b64, seconds) => p.evaluate(async ([b, secs]) => {
    const raw = atob(b), pcm = new Int16Array(raw.length / 2);
    for (let i = 0; i < pcm.length; i++) pcm[i] = raw.charCodeAt(2 * i) | (raw.charCodeAt(2 * i + 1) << 8);
    send({ cmd: 'enableMic', value: true }); await new Promise(r => setTimeout(r, 300));
    send({ cmd: 'setPTT', value: true });
    const N = 960, t0 = performance.now();
    for (let k = 0; k < secs * 50; k++) {
        const buf = new ArrayBuffer(6 + N * 2), dv = new DataView(buf);
        dv.setUint8(0, 3); dv.setUint16(2, k & 0xffff, true);
        for (let i = 0; i < N; i++) dv.setInt16(6 + 2 * i, pcm[(k * N + i) % pcm.length], true);
        sendTxAudio(buf);
        const due = t0 + (k + 1) * 20 - performance.now(); if (due > 0) await new Promise(r => setTimeout(r, due));
    }
    send({ cmd: 'setPTT', value: false }); await new Promise(r => setTimeout(r, 600));
    send({ cmd: 'enableMic', value: false });
}, [b64, seconds]);

const until = (p, fn, arg, timeout = 60000) => p.waitForFunction(fn, arg, { timeout, polling: 250 });

const SHOTS = {
    // FT8: A and C call CQ on the even slots, B answers A by clicking its CQ;
    // A's CQ: FIRST answers back, and the QSO runs to the log.
    async ft8() {
        await noise(1, 5);
        const [A, B, C] = await Promise.all([
            helper(URL_A, { call: CALL_A, grid: GRID_A, ls: { wfDigiCqAnswer: 'first' } }),
            rig(URL_B, { call: CALL_B, grid: GRID_B }),
            helper(URL_C, { call: CALL_C, grid: GRID_C })]);
        for (const p of [A, B, C]) { await p.evaluate(() => toggleDigiBar()); await until(p, () => window.digiBarVisible === true); }
        await sleep(1500);
        await A.evaluate(() => { digiTxFreq = 1240; digiCallCq(); });
        await C.evaluate(() => { digiTxFreq = 2080; digiCallCq(); });
        await B.evaluate(() => { digiRxFreq = 1240; document.getElementById('digiCqParityBtn').click(); });
        await until(B, () => !!document.querySelector('#digiRxPanel [data-msg^="CQ%20W1ABC"]'), null, 150000);
        log('  B decoded the CQ');
        await B.dblclick('#digiRxPanel [data-msg^="CQ%20W1ABC"]');
        await until(B, () => !!document.querySelector('.digi-logged-row'), null, 240000);
        log('  B logged the QSO');
        await sleep(2000);
        await shot(B, 'ft8-panel');
        for (const p of [A, B, C]) await p.evaluate(() => { const h = document.getElementById('digiHaltBtn'); if (h) h.click(); });
        await close(A, B, C);
    },

    // CW: A calls CQ, B copies it with the decoder and answers; B is taken
    // while its answer goes out, the copy above and the keyed text below.
    async cw() {
        await noise(1, 5);
        const [A, B] = await Promise.all([helper(URL_A, { call: CALL_A }), rig(URL_B, { call: CALL_B })]);
        await B.evaluate(async () => { await fetch('/api/v1/cw/macros', { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ macros: {
            1: 'CQ CQ DE [CALL] [CALL] K', 2: '[QSO] DE [CALL] K', 3: '[QSO] DE [CALL] TNX FER CALL UR [RST] [RST] BK', 4: 'TU 73 DE [CALL] EE',
            6: 'QRZ? DE [CALL] K', 7: 'AGN AGN', 8: 'NAME AL AL QTH HARTFORD CT', 9: 'RIG IC-7300 ES WFWEB ANT DIPOLE', 10: '73 ES GL' } }) }); });
        await B.reload(); await B.click('#startBtn'); await until(B, () => window.audioEnabled === true && window.currentFreq > 0, null, 30000);
        for (const p of [A, B]) {
            await p.evaluate(() => setMode('CW')); await until(p, () => window.currentMode === 'CW');
            await p.evaluate(() => renderCWOverlay()); await until(p, () => !document.getElementById('cwBar').classList.contains('hidden'));
        }
        await B.click('#cwDecoderToggle');
        await until(B, () => /DECOD/.test(document.getElementById('cwDecoderTextInner').parentNode.parentNode.textContent) || true);
        await sleep(6000);   // the model loads
        await A.evaluate(c => sendCWText('CQ CQ DE ' + c + ' ' + c + ' K'), CALL_A);
        await until(B, c => document.getElementById('cwDecoderTextInner').textContent.includes(c + ' K'), CALL_A, 90000);
        log('  B copied the CQ');
        await sleep(2500);
        await B.evaluate(([a, b]) => sendCWText(a + ' DE ' + b + ' GM TNX CALL UR 599 599 NAME AL'), [CALL_A, CALL_B]);
        await sleep(9000);
        await shot(B, 'cw-screen');
        await B.click('#cwPaddleBtn'); await sleep(600);
        await crop(B, 'cw-paddle-window', '#paddleOverlay');
        await close(A, B);
    },

    // RTTY: both bars open on AFSK; A sends a CQ from the compose line, B copies.
    async rtty() {
        await noise(1, 5);
        const [A, B] = await Promise.all([helper(URL_A, { call: CALL_A }), rig(URL_B, { call: CALL_B })]);
        for (const p of [A, B]) {
            await p.evaluate(() => setMode('RTTY'));
            await until(p, () => window.RttyPanel && RttyPanel.visible() && RttyPanel.afsk() && window.currentMode === 'LSB', null, 15000);
        }
        await until(B, () => RttyPanel.state.decoding, null, 60000);
        await sleep(1500);
        await A.fill('#rttyInput', 'CQ CQ CQ DE ' + CALL_A + ' ' + CALL_A + ' ' + CALL_A + ' PSE K');
        await A.click('#rttySendBtn');
        await until(B, c => document.getElementById('rttyText').textContent.includes('PSE K'), CALL_A, 90000);
        log('  B copied the CQ');
        await sleep(2000);
        await B.evaluate(() => { const c = document.getElementById('rttyCallInput'); if (c) { c.value = 'W1ABC'; c.dispatchEvent(new Event('input')); } });
        await A.fill('#rttyInput', 'K2DEF DE W1ABC GA OM TNX FER CALL. UR RST 599 599 NAME BOB BOB QTH BOSTON MA. HW CPY? K2DEF DE W1ABC KN');
        await A.click('#rttySendBtn');
        await until(B, () => document.getElementById('rttyText').textContent.includes('NAME BOB'), null, 90000);
        await sleep(2500);
        await shot(B, 'rtty-screen');
        await close(A, B);
    },

    // SSTV: A sends a picture in Robot 36, B decodes it into its gallery.
    async sstv() {
        await noise(1, 5);
        const [A, B] = await Promise.all([helper(URL_A, { call: CALL_A }), rig(URL_B, { call: CALL_B })]);
        for (const p of [A, B]) {
            await p.evaluate(() => toggleSstvBar());
            await until(p, () => window.SstvPanel && SstvPanel.state.modes.length > 0, null, 20000);
        }
        await A.evaluate(async () => {
            // A scene to send: sky, sun, hills, a lake.
            const cv = document.createElement('canvas'); cv.width = 640; cv.height = 480; const g = cv.getContext('2d');
            let gr = g.createLinearGradient(0, 0, 0, 300); gr.addColorStop(0, '#1d3f8f'); gr.addColorStop(0.6, '#e9875a'); gr.addColorStop(1, '#f7c86b');
            g.fillStyle = gr; g.fillRect(0, 0, 640, 300);
            g.fillStyle = '#ffe9a8'; g.beginPath(); g.arc(440, 250, 46, 0, Math.PI * 2); g.fill();
            g.fillStyle = '#3c2f4f'; g.beginPath(); g.moveTo(0, 300); g.lineTo(90, 200); g.lineTo(170, 260); g.lineTo(260, 170); g.lineTo(380, 290); g.lineTo(640, 220); g.lineTo(640, 320); g.lineTo(0, 320); g.fill();
            g.fillStyle = '#24603a'; g.beginPath(); g.moveTo(0, 330); g.quadraticCurveTo(200, 270, 400, 320); g.quadraticCurveTo(540, 350, 640, 300); g.lineTo(640, 480); g.lineTo(0, 480); g.fill();
            gr = g.createLinearGradient(0, 360, 0, 480); gr.addColorStop(0, '#5c8fd6'); gr.addColorStop(1, '#1d3f8f');
            g.fillStyle = gr; g.beginPath(); g.ellipse(330, 420, 250, 50, 0, 0, Math.PI * 2); g.fill();
            // an antenna mast on the hill
            g.strokeStyle = '#111'; g.lineWidth = 4; g.beginPath(); g.moveTo(560, 300); g.lineTo(560, 150); g.stroke();
            g.lineWidth = 3; for (const y of [160, 185, 210]) { g.beginPath(); g.moveTo(530, y); g.lineTo(590, y); g.stroke(); }
            const blob = await new Promise(r => cv.toBlob(r, 'image/png'));
            SstvPanel.setTxImage(blob, 'sunset.png');
            const sel = document.getElementById('sstvTxModeSel'); sel.value = 'Robot 36'; sel.dispatchEvent(new Event('change'));
        });
        await until(A, () => !!SstvPanel.state.txImage, null, 10000);
        await sleep(1000);
        await A.click('#sstvSendBtn');
        await until(B, () => SstvPanel.state.rx !== null, null, 30000);
        log('  B is receiving');
        await sleep(20000);
        await shot(B, 'sstv-receiving');
        await until(B, () => SstvPanel.state.rx === null && /saved/.test(SstvPanel.state.status), null, 90000);
        await B.evaluate(async () => {
            // B's reply, ready in the TX pane: a QSL-style card.
            const cv = document.createElement('canvas'); cv.width = 640; cv.height = 480; const g = cv.getContext('2d');
            const gr = g.createLinearGradient(0, 0, 640, 480); gr.addColorStop(0, '#0e2a47'); gr.addColorStop(1, '#2a7a8c');
            g.fillStyle = gr; g.fillRect(0, 0, 640, 480);
            g.strokeStyle = 'rgba(255,255,255,0.18)'; g.lineWidth = 2;
            for (let r = 60; r < 700; r += 60) { g.beginPath(); g.arc(120, 400, r, -Math.PI / 2, 0); g.stroke(); }
            g.fillStyle = '#ffd34d'; g.font = 'bold 120px sans-serif'; g.textAlign = 'center'; g.fillText('K2DEF', 330, 210);
            g.fillStyle = '#ffffff'; g.font = '34px sans-serif'; g.fillText('Hartford, CT  ·  FN31', 330, 270);
            g.font = '28px sans-serif'; g.fillText('wfweb  ·  IC-7300', 330, 320);
            const blob = await new Promise(r => cv.toBlob(r, 'image/png'));
            SstvPanel.setTxImage(blob, 'qsl.png');
            const t = document.getElementById('sstvOverlayInput'); t.value = 'TNX W1ABC  UR 595'; t.dispatchEvent(new Event('input'));
        });
        await sleep(1500);
        await shot(B, 'sstv-screen');
        await B.click('#sstvGalleryBtn'); await sleep(1200);
        await shot(B, 'sstv-gallery');
        await close(A, B);
    },

    // JS8: A sends K2DEF a directed message, B answers.
    async js8() {
        await noise(1, 5);
        const [A, B] = await Promise.all([helper(URL_A, { call: CALL_A, grid: GRID_A }), rig(URL_B, { call: CALL_B, grid: GRID_B })]);
        for (const p of [A, B]) { await p.evaluate(() => toggleJs8Bar()); await until(p, () => js8MessengerVisible() && js8Diag().rxEnabled, null, 20000); }
        await sleep(2000);
        const say = async (p, to, text) => p.evaluate(([t, m]) => {
            const d = document.getElementById('js8DxCall'); d.value = t; d.dispatchEvent(new Event('input'));
            const c = document.getElementById('js8Compose'); c.value = m; c.dispatchEvent(new Event('input'));
            document.getElementById('js8SendBtn').click();
        }, [to, text]);
        await say(A, CALL_B, 'GM AL, ARE YOU ON FOR THE NET TONIGHT?');
        await until(B, c => { const f = document.getElementById('js8Feed').textContent; return f.includes(c) && f.includes('TONIGHT'); }, CALL_A, 150000);
        log('  B copied A');
        await say(B, CALL_A, 'YES, 0000Z ON 7078. 73');
        await until(A, () => document.getElementById('js8Feed').textContent.includes('7078'), null, 150000);
        await sleep(2000);
        await shot(B, 'js8-screen');
        await close(A, B);
    },

    // Packet: A connects to B and they talk on the terminal; then APRS
    // beacons from A and C for B's station list and map.
    async packet() {
        await noise(1, 3);
        const [A, B, C] = await Promise.all([helper(URL_A, { call: CALL_A }), rig(URL_B, { call: CALL_B }), helper(URL_C, { call: CALL_C })]);
        for (const p of [A, B, C]) {
            await p.evaluate(() => Packet.show()); await sleep(800);
            await p.evaluate(() => document.getElementById('packetMode1200').click());
        }
        for (const p of [A, B]) await p.evaluate(() => document.getElementById('packetTabTerm').click());
        await sleep(1500);
        await A.evaluate(c => { const e = document.getElementById('termPeerCall'); e.value = c; e.dispatchEvent(new Event('input')); document.getElementById('termConnectBtn').click(); }, CALL_B);
        await until(B, () => document.getElementById('termStateChip').textContent === 'CONNECTED', null, 30000);
        await until(A, () => document.getElementById('termStateChip').textContent === 'CONNECTED', null, 30000);
        await sleep(3000);
        const line = async (p, t) => { await p.evaluate(x => { document.getElementById('termInput').value = x; document.getElementById('termSendBtn').click(); }, t); await sleep(3500); };
        await line(A, 'Good evening Al, testing the new antenna.');
        await line(B, 'Hi Bob, solid copy here at 1200 baud.');
        await line(A, 'Great. Anything on the BBS tonight?');
        await line(B, 'Club meeting moved to Thursday. 73');
        await sleep(3000);
        await shot(B, 'packet-terminal');
        // APRS
        for (const p of [A, B, C]) await p.evaluate(() => document.getElementById('packetTabAprs').click());
        await sleep(800);
        const beacon = async (p, lat, lon, comment) => { await p.evaluate(([la, lo, c]) => {
            for (const [id, v] of [['aprsLat', la], ['aprsLon', lo], ['aprsComment', c]]) { const e = document.getElementById(id); e.value = v; e.dispatchEvent(new Event('input')); e.dispatchEvent(new Event('change')); }
            document.getElementById('aprsTxNowBtn').click(); }, [lat, lon, comment]); await sleep(4000); };
        await beacon(A, '42.3601', '-71.0589', 'Boston club station');
        await beacon(C, '41.8240', '-71.4128', 'Mobile, 146.520 simplex');
        await beacon(B, '41.7658', '-72.6734', 'wfweb on the IC-7300');
        await until(B, () => parseInt(document.getElementById('aprsStationCount').textContent, 10) >= 2, null, 30000);
        await sleep(1500);
        await shot(B, 'aprs-stations');
        await B.click('#aprsMapHeader'); await sleep(6000);
        await shot(B, 'aprs-map');
        await close(A, B, C);
    },

    // The log window, on B's scratch logbook (a few QSOs added over REST).
    async logbook() {
        const B = await rig(URL_B, { call: CALL_B, grid: GRID_B });
        const now = Date.now();
        const qsos = [
            ['JA1XYZ', 'FT8', 14074000, '20M', '-14', '-17', 'PM95', 26 * 60], ['G4ABC', 'CW', 7031000, '40M', '599', '579', 'IO91', 23 * 60],
            ['VE3XYZ', 'SSB', 14250000, '20M', '59', '57', 'FN03', 20 * 60], ['DL2ABC', 'RTTY', 14085000, '20M', '599', '599', 'JO62', 170],
            ['EA8XYZ', 'FT4', 21140000, '15M', '+02', '-05', 'IL18', 140], ['N3GHI', 'JS8', 7078000, '40M', '+05', '-01', 'EM79', 95],
            ['VK2XYZ', 'FT8', 14074000, '20M', '-19', '-21', 'QF56', 61], ['W1ABC', 'FT8', 14074000, '20M', '-03', '-02', 'FN42', 32],
            ['W1ABC', 'RTTY', 14085000, '20M', '599', '599', 'FN42', 18], ['W1ABC', 'CW', 14040000, '20M', '599', '599', 'FN42', 7]];
        for (const [call, mode, freq, band, rstSent, rstRcvd, theirGrid, ago] of qsos) {
            const t = new Date(now - ago * 60000).toISOString();
            await B.evaluate(async q => { await fetch('/api/v1/logbook', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(q) }); },
                { call, mode, freq, band, rstSent, rstRcvd, theirGrid, grid: 'FN20',
                  date: t.slice(0, 10).replace(/-/g, ''), time: t.slice(11, 19).replace(/:/g, '') });
        }
        await B.evaluate(() => openLogModal()); await sleep(1500);
        await crop(B, 'logbook-window', '#digiLogModal');
        await close(B);
    },

    // FreeDV: RADE from A with a speech recording as the mic; B shows sync,
    // SNR and, after the over, the callsign from the end-of-over frame.
    async freedv() {
        await noise(1, 5);
        const b64 = speech();
        const [A, B] = await Promise.all([helper(URL_A, { call: CALL_A, grid: GRID_A }), rig(URL_B, { call: CALL_B, grid: GRID_B })]);
        for (const p of [A, B]) { await p.evaluate(() => toggleFreeDV()); await until(p, () => window.freedvEnabled === true && window.freedvModeName === 'RADE', null, 15000); }
        await sleep(2000);
        for (let attempt = 1; attempt <= 3; attempt++) {
            const tx = talk(A, b64, 18);
            const synced = await until(B, () => window.freedvSync === true, null, 16000).then(() => true, () => false);
            if (synced) { await sleep(12000); if (await B.evaluate(() => window.freedvSync === true)) await shot(B, 'freedv-receiving'); }
            await tx;
            const call = await until(B, c => window.radeRxCallsign === c, CALL_A, 6000).then(() => true, () => false);
            log('  attempt ' + attempt + ': sync ' + synced + ', callsign ' + call);
            if (call) { await sleep(300); await shot(B, 'freedv-callsign'); break; }
            await sleep(3000);
        }
        for (const p of [A, B]) await p.evaluate(() => { if (window.freedvEnabled) toggleFreeDV(); });
        await sleep(1000);
        await close(A, B);
    },

    // The Standalone connect page (features.sh serves the bundle on 8765).
    async standalone() {
        const ctx = await br.newContext({ viewport: { width: W, height: H }, deviceScaleFactor: 2 });
        const p = await ctx.newPage(); p.wfUrl = 'standalone'; open_.push(p);
        await p.goto('http://127.0.0.1:8765/');
        await until(p, () => !!document.querySelector('.start-card.ready'), null, 15000);
        await sleep(800);
        await shot(p, 'standalone-connect');
        await close(p);
    },

    // REC in the top bar, and its menu.
    async recording() {
        const B = await rig(URL_B, { call: CALL_B });
        await B.click('#recBtn'); await sleep(700);
        await crop(B, 'recording-menu', ['#recBtn', '.rec-menu, #recMenu'], 8);
        await close(B);
    },

    // A phone in portrait: the main screen, then the key lock.
    async phone() {
        await noise(1, 5);
        const [A, B] = await Promise.all([helper(URL_A, { call: CALL_A }), rig(URL_B, { call: CALL_B, w: 390, h: 844, mobile: true, dpr: 3 })]);
        for (const p of [A, B]) { await p.evaluate(() => { send({ cmd: 'setFrequency', value: 14205000 }); }); }
        await until(B, () => Math.abs(window.currentFreq - 14205000) < 10, null, 8000);
        await sleep(1500);
        const tx = talk(A, speech(), 14);
        await sleep(11000);
        await shot(B, 'phone-main');
        await tx;
        await B.click('#uiLockBtn'); await sleep(800);
        await shot(B, 'phone-locked');
        await close(A, B);
    },

    // The main screen: B on 20 m SSB while A talks.
    async home() {
        await noise(1, 5);
        const [A, B] = await Promise.all([helper(URL_A, { call: CALL_A }), rig(URL_B, { call: CALL_B })]);
        for (const p of [A, B]) { await p.evaluate(() => { send({ cmd: 'setFrequency', value: 14205000 }); }); }
        await until(B, () => Math.abs(window.currentFreq - 14205000) < 10, null, 8000);
        await sleep(1500);
        const tx = talk(A, speech(), 14);
        await sleep(11000);
        await shot(B, 'home-main-screen');
        await tx;
        await close(A, B);
    },
};

const names = only.length ? only : Object.keys(SHOTS);
for (const n of names) {
    if (!SHOTS[n]) { log('no shot ' + n); continue; }
    log(n);
    try { await SHOTS[n](); } catch (e) {
        log('  FAILED ' + n + ': ' + (e.stack || e));
        for (const [i, p] of open_.entries()) await p.screenshot({ path: png('FAILED-' + n + '-' + i) }).catch(() => {});
        while (open_.length) await close(open_[0]).catch(() => open_.shift());
    }
}
await br.close();
