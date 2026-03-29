importScripts('wspr-decoder-wasm.js');

let modulePromise = null;
let api = null;

function ensureApi() {
    if (api) return Promise.resolve(api);
    if (!modulePromise) {
        modulePromise = WsprDecoderModule().then(function(module) {
            const wrapped = {
                module: module,
                init: module.cwrap('wfweb_wspr_init', 'number', []),
                decode: module.cwrap('wfweb_wspr_decode', 'number', ['number', 'number', 'number', 'number', 'number']),
                clearHashes: module.cwrap('wfweb_wspr_clear_hashes', null, []),
                getCount: module.cwrap('wfweb_wspr_get_result_count', 'number', []),
                getErrorPtr: module.cwrap('wfweb_wspr_get_error', 'number', []),
                getFreqMHz: module.cwrap('wfweb_wspr_get_result_freq_mhz', 'number', ['number']),
                getSnr: module.cwrap('wfweb_wspr_get_result_snr', 'number', ['number']),
                getDt: module.cwrap('wfweb_wspr_get_result_dt', 'number', ['number']),
                getSync: module.cwrap('wfweb_wspr_get_result_sync', 'number', ['number']),
                getDrift: module.cwrap('wfweb_wspr_get_result_drift', 'number', ['number']),
                getDbm: module.cwrap('wfweb_wspr_get_result_dbm', 'number', ['number']),
                getMessagePtr: module.cwrap('wfweb_wspr_get_result_message', 'number', ['number']),
                getCallPtr: module.cwrap('wfweb_wspr_get_result_callsign', 'number', ['number']),
                getGridPtr: module.cwrap('wfweb_wspr_get_result_grid', 'number', ['number']),
                malloc: module.cwrap('malloc', 'number', ['number']),
                free: module.cwrap('free', null, ['number'])
            };
            wrapped.init();
            api = wrapped;
            return wrapped;
        }).catch(function(err) {
            modulePromise = null;
            api = null;
            throw err;
        });
    }
    return modulePromise;
}

function ptrToString(apiRef, ptr) {
    if (!ptr) return '';
    try { return apiRef.module.UTF8ToString(ptr) || ''; } catch (e) { return ''; }
}

function formatUtc(slotStartMs) {
    return new Date(slotStartMs).toISOString().substr(11, 8) + 'Z';
}

function formatDate6(slotStartMs) {
    const d = new Date(slotStartMs);
    const yy = String(d.getUTCFullYear()).slice(-2);
    const mm = String(d.getUTCMonth() + 1).padStart(2, '0');
    const dd = String(d.getUTCDate()).padStart(2, '0');
    return yy + mm + dd;
}

function formatTime4(slotStartMs) {
    const d = new Date(slotStartMs);
    return String(d.getUTCHours()).padStart(2, '0') + String(d.getUTCMinutes()).padStart(2, '0');
}

function buildSpotId(spot) {
    return [
        'wspr',
        String(spot.slotIndex),
        String(spot.freqHz),
        spot.call || '',
        spot.grid || '',
        String(spot.dbm),
        String(spot.drift)
    ].join(':');
}

function decodeWindow(apiRef, data, samples) {
    const byteLength = samples.length * 4;
    const ptr = apiRef.malloc(byteLength);
    if (!ptr) throw new Error('malloc failed');

    const dialFrequencyHz = Number(data.dialFrequencyHz || 0);
    const audioFrequencyHz = Number(data.audioFrequencyHz || 1500);
    const wideband = data.wideband ? 1 : 0;
    const slotStartMs = Number(data.slotStartMs || 0);
    const slotIndex = Number(data.slotIndex || 0);

    try {
        apiRef.module.HEAPF32.set(samples, ptr >> 2);
        const rc = apiRef.decode(ptr, samples.length, dialFrequencyHz / 1000000.0, audioFrequencyHz, wideband);
        const errText = ptrToString(apiRef, apiRef.getErrorPtr());
        if (!rc && errText) throw new Error(errText);

        const count = apiRef.getCount();
        const date = formatDate6(slotStartMs);
        const time = formatTime4(slotStartMs);
        const spots = [];

        for (let i = 0; i < count; i++) {
            const freqMHz = apiRef.getFreqMHz(i);
            const freqHz = Math.round(freqMHz * 1000000);
            const snr = Number(apiRef.getSnr(i));
            const dt = Number(apiRef.getDt(i));
            const sync = Number(apiRef.getSync(i));
            const drift = Number(apiRef.getDrift(i));
            const dbm = Number(apiRef.getDbm(i));
            const call = ptrToString(apiRef, apiRef.getCallPtr(i)).trim();
            const grid = ptrToString(apiRef, apiRef.getGridPtr(i)).trim();
            const message = ptrToString(apiRef, apiRef.getMessagePtr(i)).trim();
            const spot = {
                date: date,
                time: time,
                sync: Math.round(sync * 10),
                syncRaw: sync,
                snr: snr,
                dt: dt,
                drift: drift,
                freq: freqMHz,
                freqHz: freqHz,
                dialFrequencyHz: dialFrequencyHz,
                audioFrequencyHz: audioFrequencyHz,
                message: message,
                text: message,
                call: call,
                grid: grid,
                dbm: dbm,
                slotIndex: slotIndex,
                slotStartMs: slotStartMs
            };
            spot.meta = snr.toFixed(0) + ' dB | ' + dt.toFixed(1) + ' s | ' + freqMHz.toFixed(6) + ' MHz | drift ' + drift;
            spot.spotId = buildSpotId(spot);
            spots.push(spot);
        }

        return {
            ok: true,
            stub: false,
            epoch: Number(data.epoch || 0),
            slotIndex: slotIndex,
            slotStartMs: slotStartMs,
            dialFrequencyHz: dialFrequencyHz,
            audioFrequencyHz: audioFrequencyHz,
            reporter: data.reporter || null,
            sampleCount: samples.length,
            spots: spots,
            summary: formatUtc(slotStartMs) + ' ' + spots.length + ' spot' + (spots.length === 1 ? '' : 's')
        };
    } finally {
        apiRef.free(ptr);
    }
}

self.onmessage = function(ev) {
    const data = ev.data || {};
    if (data.type !== 'decode') return;

    const samples = new Float32Array(data.samples || 0);
    const started = Date.now();

    ensureApi()
        .then(function(apiRef) {
            const result = decodeWindow(apiRef, data, samples);
            result.decodeMs = Date.now() - started;
            self.postMessage(result);
        })
        .catch(function(err) {
            self.postMessage({
                ok: false,
                epoch: Number(data.epoch || 0),
                slotIndex: Number(data.slotIndex || 0),
                slotStartMs: Number(data.slotStartMs || 0),
                error: String(err)
            });
        });
};
