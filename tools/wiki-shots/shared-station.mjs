// Driven by shared-station.py: node shared-station.mjs <phase> <url> <out dir> <state json>
import fs from 'node:fs'; import os from 'node:os'; import path from 'node:path'; import { createRequire } from 'node:module';
const [phase, url, out, stateJson] = process.argv.slice(2);
const st = JSON.parse(stateJson || '{}');
const npx = path.join(os.homedir(), '.npm', '_npx'); let core = null;
for (const d of fs.readdirSync(npx)) { const p = path.join(npx, d, 'node_modules', 'playwright-core'); if (fs.existsSync(p)) core = p; }
const { chromium } = createRequire(import.meta.url)(core);
// Playwright's own Chromium when it is there, else the system's.
const base = path.join(os.homedir(), '.cache', 'ms-playwright');
const rev = fs.existsSync(base) ? fs.readdirSync(base).filter(d => /^chromium-\d+$/.test(d)).sort().pop() : null;
const exe = (rev ? ['chrome-linux64/chrome', 'chrome-linux/chrome'].map(s => path.join(base, rev, s)) : [])
    .concat(['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser']).find(fs.existsSync);
const br = await chromium.launch({ executablePath: exe });
const W = 1000, H = 680;
const mk = async (name, w = W, h = H, start = true) => {
    const ctx = await br.newContext({ ignoreHTTPSErrors: true, viewport: { width: w, height: h }, deviceScaleFactor: 2 });
    if (name) await ctx.addCookies([{ name: 'wfweb_session', value: st.cookies[name], url }]);
    const p = await ctx.newPage();
    p.on('pageerror', e => console.log('PAGEERROR', name, e.message));
    await p.goto(url);
    if (start) { await p.click('#startBtn'); await p.waitForTimeout(1500); }
    return p;
};
const wait = (p, ms) => p.waitForTimeout(ms);
const png = n => path.join(out, n + '.png');
// Crop: the union of the given elements, padded.
const crop = async (p, name, sels, pad = 6) => {
    const boxes = [];
    for (const s of [].concat(sels)) { const b = await p.locator(s).boundingBox(); if (b) boxes.push(b); }
    const x0 = Math.max(0, Math.min(...boxes.map(b => b.x)) - pad), y0 = Math.max(0, Math.min(...boxes.map(b => b.y)) - pad);
    const x1 = Math.max(...boxes.map(b => b.x + b.width)) + pad, y1 = Math.max(...boxes.map(b => b.y + b.height)) + pad;
    await p.screenshot({ path: png(name), clip: { x: x0, y: y0, width: x1 - x0, height: y1 - y0 } });
    console.log('  ' + name);
};
// The seat end of the strip.
const seat = async (p, name) => {
    await crop(p, name, '#seatGroup', 5);
    console.log('      [' + await p.evaluate(() => document.getElementById('seatWho').textContent + ' | ' + (document.getElementById('seatTag').style.display === 'none' ? '-' : document.getElementById('seatTag').textContent)) + ']');
};
// A window, cut under its last line instead of at its fixed height.
const win = async (p, name, sel) => {
    const b = await p.locator(sel).boundingBox();
    const bottom = await p.evaluate(s => { let m = 0; document.querySelectorAll(s + ' .shared-panel > *').forEach(e => { m = Math.max(m, e.getBoundingClientRect().bottom); }); return m; }, sel);
    await p.screenshot({ path: png(name), clip: { x: b.x, y: b.y, width: b.width, height: Math.min(b.height, bottom + 18 - b.y) } });
    console.log('  ' + name);
};
const api = (p, method, route, body) => p.evaluate(async ([m, r, b]) => {
    const res = await fetch(r, { method: m, headers: { 'Content-Type': 'application/json' }, body: b ? JSON.stringify(b) : undefined });
    return { status: res.status, body: await res.json().catch(() => null) };
}, [method, route, body]);

if (phase === 'setup') {
    const p = await mk(null, 520, 560, false); await wait(p, 1500);
    await p.fill('#loginCode', 'K7QX-4MZP').catch(() => {});
    await crop(p, 'setup-sheet', '#loginForm', 14);
} else {
    // --- sheets and the visitor ---
    let p = await mk(null, 520, 560, false); await wait(p, 1500);
    await crop(p, 'login-sheet', '#loginForm', 14);
    await p.context().close();

    const admin = await mk('clubadmin');
    const me = await mk('tom');          // the reader's point of view
    const other = await mk('maria');

    // --- the seat, state by state ---
    await seat(me, 'seat-free');
    await me.evaluate(() => send({ cmd: 'setFrequency', value: 14074000 })); await wait(me, 250);
    await crop(me, 'toast-take-first', '#accessToast', 8);
    await wait(me, 2600);
    await me.click('#seatTag'); await wait(me, 600);
    await seat(me, 'seat-yours');
    await me.click('#seatTag'); await wait(me, 300);
    await crop(me, 'seat-menu', ['#seatGroup', '#seatMenu'], 8);
    await me.evaluate(() => hideSeatMenu());
    // on the air under the holder's own call (and onto 20 m, for the band inhibit below)
    await me.evaluate(() => send({ cmd: 'setFrequency', value: 14074000 })); await wait(me, 600);
    const tomId = (await api(admin, 'GET', '/api/v1/shared/users')).body.users.find(u => u.name === 'tom').id;
    await api(admin, 'PUT', '/api/v1/shared/users/' + tomId, { onAir: 'own' }); await wait(me, 900);
    await seat(me, 'seat-own-call');
    await api(admin, 'PUT', '/api/v1/shared/users/' + tomId, { onAir: 'station' }); await wait(me, 700);
    await me.evaluate(() => seatAction('seatRelease')); await wait(me, 600);

    await other.click('#seatTag'); await wait(me, 700);
    await seat(me, 'seat-held');
    await me.evaluate(() => send({ cmd: 'setFrequency', value: 14074000 })); await wait(me, 250);
    await crop(me, 'toast-held', '#accessToast', 8);
    await wait(me, 2600);
    await me.click('#seatTag'); await wait(me, 800);
    await seat(me, 'seat-asked');
    await crop(other, 'prompt-request', '#seatPrompt', 6);
    await other.click('#seatPromptNo'); await wait(me, 700);
    await seat(me, 'seat-in-line');
    await other.evaluate(() => seatAction('seatRelease')); await wait(me, 900);
    await seat(me, 'seat-offer');
    await crop(me, 'prompt-offer', '#seatPrompt', 6);
    await me.click('#seatPromptNo'); await wait(me, 600);

    // grace: the holder's connection drops
    await other.click('#seatTag'); await wait(me, 700);
    await other.context().close(); await wait(me, 1500);
    await seat(me, 'seat-grace');

    // --- the YOU end ---
    await crop(me, 'you-tag', '#userTag', 6);
    await admin.click('#userTag'); await wait(admin, 300);
    await crop(admin, 'you-menu', ['#userTag', '#userMenu'], 8);
    await admin.evaluate(() => hideUserMenu());
    await me.evaluate(() => openPasswordModal()); await wait(me, 500);
    await crop(me, 'password-sheet', '#passwordOverlay', 0);
    await me.evaluate(() => closePasswordModal());

    // --- the schedule, then the settings window ---
    const day = 86400e3, at = (d, h) => { const t = new Date(Date.now() + d * day); t.setHours(h, 0, 0, 0); return t.toISOString(); };
    console.log('  slots', (await api(admin, 'POST', '/api/v1/shared/reservations', { username: 'maria', start: at(1, 20), minutes: 60, repeat: 'weekly', title: 'Club net control' })).status,
        (await api(admin, 'POST', '/api/v1/shared/reservations', { username: 'tom', start: at(2, 18), minutes: 120, repeat: 'once', title: 'Contest practice' })).status);
    await wait(me, 35000);                 // the grace period ends, the seat is free again
    const again = await mk('maria'); await again.click('#seatTag'); await wait(again, 600);   // someone in the NOW tab
    await me.click('#userTag'); await wait(me, 300); await me.click('#userMenuSchedule'); await wait(me, 1000);
    await win(me, 'schedule-window', '#scheduleOverlay');
    await me.evaluate(() => closeScheduleModal());
    await admin.setViewportSize({ width: W, height: 1500 }); await admin.evaluate(() => openSharedModal()); await wait(admin, 1200);
    for (const t of ['now', 'accounts', 'schedule', 'rules']) {
        await admin.evaluate(t => sharedSetTab(t), t); await wait(admin, 400);
        await admin.evaluate(() => { for (const e of [document.getElementById('sharedOverlay'), document.querySelector('#sharedOverlay .shared-panel')]) { e.style.maxHeight = 'none'; e.style.height = 'auto'; e.style.overflow = 'visible'; } });
        await win(admin, 'settings-' + t, '#sharedOverlay');
    }
    await admin.evaluate(() => closeSharedModal());

    // --- where the strip sits, on a phone ---
    const ph = await mk('tom', 390, 760); await wait(ph, 800);
    await ph.screenshot({ path: png('phone-top'), clip: { x: 0, y: 0, width: 390, height: 170 } }); console.log('  phone-top');
    await ph.context().close();

    // --- a private session, seen by someone else ---
    await api(admin, 'PUT', '/api/v1/shared/policy', { allowPrivate: true }); await wait(again, 500);
    await again.evaluate(() => seatAction('seatPrivate', { value: true })); await wait(me, 1200);
    await crop(me, 'seat-private', ['#stationStrip', '#seatHiddenBanner'], 0);
    await again.evaluate(() => seatAction('seatPrivate', { value: false }));
    await api(admin, 'PUT', '/api/v1/shared/policy', { allowPrivate: false }); await wait(me, 600);

    // --- TX inhibit, visitors ---
    await api(admin, 'PUT', '/api/v1/shared/policy', { txInhibit: true }); await wait(me, 700);
    await crop(me, 'tx-inhibit', '#stationStrip', 0);
    await api(admin, 'PUT', '/api/v1/shared/policy', { txInhibit: false, txInhibitBands: ['20m'] }); await wait(me, 900);
    await crop(me, 'tx-inhibit-band', '#stationStrip', 0);
    console.log('      [' + await me.evaluate(() => document.getElementById('txInhibitTag').textContent) + ']');
    await api(admin, 'PUT', '/api/v1/shared/policy', { txInhibitBands: [] });
    await api(admin, 'PUT', '/api/v1/shared/policy', { txInhibit: false, anonymous: 'listen' }); await wait(me, 500);
    const v = await mk(null); await wait(v, 500);
    await crop(v, 'you-visitor', '#userTag', 6);
    await v.context().close();
    await api(admin, 'PUT', '/api/v1/shared/policy', { anonymous: 'none' });

    // --- a reserved slot running, its owner away ---
    await again.evaluate(() => seatAction('seatRelease')); await again.context().close(); await wait(me, 800);
    const r = await api(admin, 'POST', '/api/v1/shared/reservations', { username: 'lee', start: new Date(Date.now() - 60e3).toISOString(), minutes: 30, repeat: 'once', title: 'Sked' });
    console.log('  running slot', r.status, JSON.stringify(r.body).slice(0, 120)); await wait(me, 2500);
    await seat(me, 'seat-reserved');

}
await br.close();
