// Drives resources/web-shared/setmenu.js under node against a fake radio:
// the codec of each item type, reads (data, NG = not on this radio, no
// answer), and the checked write -- nothing is written when the read before
// it fails or no longer matches the window, a refused set and a value that
// does not stick are reported, a write of the value already there sends no
// set.  Also checks every table in resources/web-shared/setmenu/.
// Exit code 1 on a failed check.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const W = {};
new Function('window', fs.readFileSync(path.join(here, '../resources/web-shared/setmenu.js'), 'utf8'))(W);
const { WfSetMenuCodec: C, WfSetMenu } = W;

let failures = 0;
function check(ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) failures++; }

// --- codec -------------------------------------------------------------
const tot = { item: '0029', type: 'enum', options: [['00', 'OFF'], ['01', '3 min'], ['05', '30 min']] };
const sql = { item: '0061', type: 'bool' };
const lvl = { item: '0064', type: 'level', min: 0, max: 255, percent: true };
const dly = { item: '0191', type: 'number', digits: 2, min: 0, max: 20, scale: 0.1, unit: 's' };
const raw = { item: '0094', type: 'raw' };

check(C.decode(tot, '05').value === '05' && C.decode(tot, '05').text === '30 min', 'enum decodes to its label');
check(C.decode(sql, '01').text === 'ON', 'bool has default OFF / ON labels');
check(C.decode(lvl, '02 55').value === 255 && C.decode(lvl, '0255').text === '100% (255)', 'level: 4 BCD digits, percent');
check(!C.decode(lvl, '02 5A').ok, 'level: non-BCD data is an error');
check(!C.decode(lvl, '25').ok, 'level: wrong length is an error');
check(C.decode(dly, '15').text === '1.5 s', 'number: scale and unit');
check(C.decode(raw, '20 26 11 27').text === '20 26 11 27', 'raw is shown as hex');
check(C.encode(lvl, 128) === '0128', 'level encodes as BCD');
check(C.encode(dly, 7) === '07', 'number encodes with its digit count');
check(C.encode(tot, '01') === '01', 'enum encodes its code');
let threw = 0;
for (const [it, v] of [[lvl, 256], [lvl, -1], [lvl, 1.5], [tot, '02'], [raw, '00'], [dly, 21]]) {
  try { C.encode(it, v); } catch (e) { threw++; }
}
check(threw === 6, 'out-of-range, unknown codes and raw items are refused');
check(C.readHex(sql) === '1A 05 00 61' && C.setHex(sql, '01') === '1A 05 00 61 01', 'read / set frames');

// --- fields in the radio's own units --------------------------------------
{
  const nbw = { item: '0190', type: 'level', digits: 4, min: 0, max: 255, range: [1, 100] };
  check(C.toShown(lvl, 128) === 50 && C.fromShown(lvl, 50) === 128, 'percent level: field 50 <-> raw 128');
  check(C.toShown(nbw, 128) === 51, 'range level: raw 128 shows as 51 on a 1-100 scale');
  check(C.toShown(dly, 15) === 1.5 && C.fromShown(dly, 1.5) === 15, 'scaled number: field 1.5 s <-> raw 15');
  let stable = true;
  for (let s = 0; s <= 100; s++) if (C.toShown(lvl, C.fromShown(lvl, s)) !== s) stable = false;
  for (let s = 1; s <= 100; s++) if (C.toShown(nbw, C.fromShown(nbw, s)) !== s) stable = false;
  for (let s = 0; s <= 20; s++) if (C.toShown(dly, C.fromShown(dly, s / 10)) !== s / 10) stable = false;
  check(stable, 'every value a field takes comes back as itself after raw and back');
  check(String(C.shownRange(lvl)) === '0,100' && String(C.shownRange(nbw)) === '1,100' && String(C.shownRange(dly)) === '0,2',
        'field limits are in shown units');
}

// --- fake radio ----------------------------------------------------------
function radio(regs) {
  const r = { regs: { ...regs }, log: [], refuseSet: new Set(), ignoreSet: new Set(), silent: new Set() };
  r.civ = async (hex) => {
    const b = hex.replace(/\s/g, '').toUpperCase();
    r.log.push(b);
    if (!b.startsWith('1A05')) return { reply: 'ng' };
    const item = b.slice(4, 8), data = b.slice(8);
    if (r.silent.has(item)) return { reply: 'timeout', error: 'no reply' };
    if (!(item in r.regs)) return { reply: 'ng' };
    if (data) {
      if (r.refuseSet.has(item)) return { reply: 'ng' };
      if (!r.ignoreSet.has(item)) r.regs[item] = data;
      return { reply: 'ok' };
    }
    return { reply: 'data', data: r.regs[item] };
  };
  return r;
}
const sets = (r) => r.log.filter((x) => x.length > 8);

const table = { model: 'TEST', groups: [{ path: 'SET > A', items: [sql, tot, lvl] }, { path: 'SET > B', items: [dly, { item: '0999', type: 'bool' }] }] };

// --- reads ---------------------------------------------------------------
{
  const rig = radio({ '0061': '00', '0029': '01', '0064': '0128', '0191': '05' });
  const m = new WfSetMenu({ civ: rig.civ });
  m.setTable(table);
  let progress = 0;
  await m.readAll((d, t) => { progress = d; });
  check(progress === 5, 'READ ALL reads every item');
  check(m.state['0061'].status === 'ok' && m.state['0061'].value === '00', 'bool read');
  check(m.state['0064'].value === 128, 'level read');
  check(m.state['0999'].status === 'missing', 'NG to a read: not on this radio');
  check(sets(rig).length === 0, 'READ ALL writes nothing');

  rig.silent.add('0029');
  await m.read('0029');
  check(m.state['0029'].status === 'error', 'no answer is an error, not a value');
}

// --- checked writes ------------------------------------------------------
{
  const rig = radio({ '0061': '00', '0029': '01', '0064': '0128', '0191': '05' });
  const m = new WfSetMenu({ civ: rig.civ });
  m.setTable(table);

  let res = await m.write('0061', '01');
  check(!res.ok && sets(rig).length === 0, 'no write before the item was read');

  await m.readAll();
  rig.log.length = 0;
  res = await m.write('0061', '01');
  check(res.ok && rig.regs['0061'] === '01', 'write: radio holds the new value');
  check(rig.log.join(',') === '1A050061,1A05006101,1A050061', 'write is read, set, read back');
  check(m.state['0061'].written === true && m.state['0061'].value === '01', 'window shows the value read back');

  // The radio changed between the window's read and the write.
  rig.regs['0029'] = '05';
  rig.log.length = 0;
  res = await m.write('0029', '00');
  check(!res.ok && sets(rig).length === 0, 'changed on the radio: nothing written');
  check(m.state['0029'].value === '05', 'window now shows what the radio holds');

  // Same value: no set at all.
  rig.log.length = 0;
  res = await m.write('0029', '05');
  check(res.ok && res.unchanged && sets(rig).length === 0, 'writing the value already there sends no set');

  // Refused by the radio.
  rig.refuseSet.add('0064');
  res = await m.write('0064', 200);
  check(!res.ok && /NG/.test(res.error) && rig.regs['0064'] === '0128', 'NG to a set is reported, value unchanged');
  check(m.state['0064'].value === 128, 'after a refused set the window shows the radio\'s value');

  // Accepted but did not stick.
  rig.ignoreSet.add('0191');
  res = await m.write('0191', 9);
  check(!res.ok && /read back/.test(res.error), 'a value that does not stick is reported');

  // Read before the write fails: nothing written.
  rig.silent.add('0061');
  rig.log.length = 0;
  res = await m.write('0061', '00');
  check(!res.ok && sets(rig).length === 0 && rig.regs['0061'] === '01', 'failed read before write: nothing written');

  // Out of range never reaches the radio.
  rig.log.length = 0;
  res = await m.write('0064', 999);
  check(!res.ok && rig.log.length === 0, 'out-of-range value is refused before any CI-V');
}

// --- lost replies --------------------------------------------------------
{
  const rig = radio({ '0061': '00', '0029': '01' });
  // Drop the first two answers to each read of 0061, as the USB link
  // sometimes does; and the OK of one set of 0029.
  let dropReads = 2, dropSetAck = true;
  const lossy = async (hex) => {
    const b = hex.replace(/\s/g, '');
    const r = await rig.civ(hex);
    if (b === '1A050061' && dropReads > 0) { dropReads--; return { reply: 'timeout', error: 'no reply' }; }
    if (b.startsWith('1A050029') && b.length > 8 && dropSetAck) { dropSetAck = false; return { reply: 'timeout', error: 'no reply' }; }
    return r;
  };
  const m = new WfSetMenu({ civ: lossy });
  m.setTable({ groups: [{ items: [sql, tot] }] });
  await m.read('0061');
  check(m.state['0061'].status === 'ok' && m.state['0061'].value === '00', 'a read that lost two replies is asked again and succeeds');
  await m.read('0029');
  rig.log.length = 0;
  const res = await m.write('0029', '05');
  check(res.ok && res.lostAck && rig.regs['0029'] === '05' && sets(rig).length === 1,
        'a set whose OK was lost is confirmed by reading, not sent twice');
  rig.silent.add('0061');
  rig.log.length = 0;
  await m.read('0061');
  check(m.state['0061'].status === 'error' && rig.log.length === 3, 'a read is tried three times in all, then reported');
}

// --- the queue: newest section first, closed sections dropped ------------
{
  const regs = {};
  for (let i = 1; i <= 12; i++) regs[String(i).padStart(4, '0')] = '00';
  const rig = radio(regs);
  const order = [];
  let release;
  let gate = new Promise((r) => { release = r; });
  // The first read is held on the wire until released, so the rest queue up.
  const held = async (hex) => { order.push(hex.replace(/\s/g, '').slice(4, 8)); if (order.length === 1) await gate; return rig.civ(hex); };
  const items = Object.keys(regs).map((n) => ({ item: n, name: 'x' + n, type: 'bool' }));
  const m = new WfSetMenu({ civ: held });
  m.setTable({ groups: [{ items }] });
  const a = m.readMany(['0001', '0002', '0003', '0004']);           // section A opened
  const b = m.readMany(['0005', '0006', '0007'], { front: true });   // then section B
  check(m.readPending('0002') && m.readPending('0005'), 'waiting reads are known as pending');
  const n = m.cancelReads(['0002', '0003', '0004']);                 // A closed
  check(n === 3, 'closing a section drops its three waiting reads');
  const c = m.readMany(['0006', '0008'], { front: true });           // C opened, shares 0006 with B
  release();
  const ra = await Promise.all(a), rb = await Promise.all(b), rc = await Promise.all(c);
  check(order.join(',') === '0001,0006,0008,0005,0007',
        'the read on the wire finishes, then the newest section, then the one before: ' + order.join(','));
  check(ra.slice(1).every((r) => r.cancelled) && ra[0].ok, 'dropped reads resolve as cancelled, the one on the wire as read');
  check(rb.every((r) => r.ok) && rc.every((r) => r.ok), 'reads moved up still answer every caller');
  check(m.state['0002'].status === 'idle', 'a dropped read leaves its item unread, to be read when opened again');

  // A write goes ahead of reads still waiting.
  order.length = 0;
  gate = new Promise((r) => { release = r; });
  m.readMany(['0009', '0010', '0011']);
  const w = m.write('0001', '01');
  release();
  await w;
  check(order[0] === '0009' && order[1] === '0001', 'the operator\'s write goes right after the read on the wire');
}

// --- serial: two writes at once do not interleave ------------------------
{
  const rig = radio({ '0061': '00', '0029': '01' });
  let inFlight = 0, overlap = false;
  const slow = async (hex) => { inFlight++; if (inFlight > 1) overlap = true; await new Promise((r) => setTimeout(r, 5)); const x = await rig.civ(hex); inFlight--; return x; };
  const m = new WfSetMenu({ civ: slow });
  m.setTable({ groups: [{ items: [sql, tot] }] });
  await m.readAll();
  await Promise.all([m.write('0061', '01'), m.write('0029', '05')]);
  check(!overlap && rig.regs['0061'] === '01' && rig.regs['0029'] === '05', 'operations run one at a time');
}

// --- the shipped tables --------------------------------------------------
const dir = path.join(here, '../resources/web-shared/setmenu');
for (const f of fs.readdirSync(dir).filter((x) => x.endsWith('.json'))) {
  let t, ok = true, why = '';
  try { t = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8')); } catch (e) { ok = false; why = e.message; }
  const seen = new Set();
  if (ok) {
    if (!t.model || !Array.isArray(t.groups)) { ok = false; why = 'needs model and groups'; }
    for (const g of t.groups || []) for (const it of g.items || []) {
      if (!/^[0-9]{4}$/.test(it.item)) { ok = false; why = 'item number ' + it.item; }
      if (seen.has(it.item)) { ok = false; why = 'duplicate ' + it.item; }
      seen.add(it.item);
      if (!['bool', 'enum', 'level', 'number', 'raw'].includes(it.type)) { ok = false; why = it.item + ' type ' + it.type; }
      if (!it.name) { ok = false; why = it.item + ' has no name'; }
      if (it.type === 'enum' && !(it.options || []).every((o) => /^[0-9A-F]{2}$/i.test(o[0]) && o[1])) { ok = false; why = it.item + ' options'; }
      if ((it.type === 'level' || it.type === 'number') && (it.digits || 4) % 2) { ok = false; why = it.item + ' odd digit count'; }
    }
  }
  check(ok, 'table ' + f + (ok ? ' (' + seen.size + ' items)' : ': ' + why));
}

console.log(failures ? failures + ' check(s) failed' : 'all checks passed');
process.exit(failures ? 1 : 0);
