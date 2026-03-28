(function(global) {
    'use strict';

    var WSPR_BIT_COUNT = 162;
    var WSPR_MESSAGE_SIZE = 11;
    var WSPR_SYMBOL_COUNT = 162;
    var WSPR_SAMPLE_RATE = 12000;
    var WSPR_SYMBOL_SAMPLES = 8192;
    var WSPR_TONE_SPACING = WSPR_SAMPLE_RATE / WSPR_SYMBOL_SAMPLES;
    var VALID_POWER_LEVELS = [0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60];
    var SYNC_VECTOR = [
        1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0,
        0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 1,
        0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0,
        1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1,
        0, 0, 1, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1,
        1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0
    ];

    function sanitizeCallsign(callsign) {
        if (callsign == null) throw new Error('callsign is required');
        callsign = String(callsign).trim().toUpperCase();
        if (!callsign) throw new Error('callsign is required');

        for (var i = 0; i < callsign.length; i++) {
            var ch = callsign[i];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch === ' ')) {
                throw new Error('WSPR type-1 callsign only supports letters and digits');
            }
        }

        if (callsign.length > 6) callsign = callsign.substring(0, 6);
        while (callsign.length < 6) callsign += ' ';

        if (callsign[1] >= '0' && callsign[1] <= '9' && (callsign[2] < '0' || callsign[2] > '9')) {
            callsign = ' ' + callsign.substring(0, 5);
        }

        if (callsign[2] < '0' || callsign[2] > '9') {
            throw new Error('WSPR type-1 callsign must have a digit in the third position');
        }

        for (var j = 3; j < 6; j++) {
            var suffixChar = callsign[j];
            if (suffixChar !== ' ' && (suffixChar < 'A' || suffixChar > 'Z')) {
                throw new Error('WSPR type-1 suffix must contain only letters');
            }
        }

        return callsign;
    }

    function sanitizeLocation(location) {
        if (location == null) throw new Error('location must contain exactly four characters');
        location = String(location).trim().toUpperCase();
        if (location.length !== 4) throw new Error('location must contain exactly four characters');

        for (var i = 0; i < 2; i++) {
            if (location[i] < 'A' || location[i] > 'R') throw new Error('location must start with two letters A-R');
        }
        for (var j = 2; j < 4; j++) {
            if (location[j] < '0' || location[j] > '9') throw new Error('location must end with two digits 0-9');
        }
        return location;
    }

    function sanitizePower(power) {
        power = Number(power);
        if (!isFinite(power)) throw new Error('power must be finite');

        var sanitized = 0;
        for (var i = 0; i < VALID_POWER_LEVELS.length; i++) {
            if (power >= VALID_POWER_LEVELS[i]) sanitized = VALID_POWER_LEVELS[i];
        }
        return sanitized;
    }

    function getPowerDescription(db) {
        var mw = Math.pow(10, db / 10);
        var label = mw < 1000 ? (db + ' dBm (' + mw.toFixed(0) + ' mW)') : (db + ' dBm (' + (mw / 1000).toFixed(0) + ' W)');
        return label.replace('01 ', '00 ');
    }

    function wsprCode(ch) {
        if (ch >= '0' && ch <= '9') return ch.charCodeAt(0) - 48;
        if (ch === ' ') return 36;
        if (ch >= 'A' && ch <= 'Z') return ch.charCodeAt(0) - 55;
        throw new Error('character ' + ch + ' is not allowed');
    }

    function getMessageBytes(callsign, location, power) {
        callsign = sanitizeCallsign(callsign);
        location = sanitizeLocation(location);
        power = sanitizePower(power);

        var call = callsign.split('');
        var intA = wsprCode(call[0]);
        intA = intA * 36 + wsprCode(call[1]);
        intA = intA * 10 + wsprCode(call[2]);
        intA = intA * 27 + (wsprCode(call[3]) - 10);
        intA = intA * 27 + (wsprCode(call[4]) - 10);
        intA = intA * 27 + (wsprCode(call[5]) - 10);

        var loc = location.split('');
        var intB = 180 * (179 - 10 * (loc[0].charCodeAt(0) - 65) - (loc[2].charCodeAt(0) - 48)) +
            10 * (loc[1].charCodeAt(0) - 65) +
            (loc[3].charCodeAt(0) - 48);
        intB = intB * 128 + power + 64;

        var bytes = new Uint8Array(7);
        bytes[3] = (intA & 0x0F) << 4;
        intA = Math.floor(intA / 16);
        bytes[2] = intA & 0xFF;
        intA = Math.floor(intA / 256);
        bytes[1] = intA & 0xFF;
        intA = Math.floor(intA / 256);
        bytes[0] = intA & 0xFF;

        bytes[6] = (intB & 0x03) << 6;
        intB = Math.floor(intB / 4);
        bytes[5] = intB & 0xFF;
        intB = Math.floor(intB / 256);
        bytes[4] = intB & 0xFF;
        intB = Math.floor(intB / 256);
        bytes[3] |= intB & 0x0F;
        return bytes;
    }

    function convolve(data) {
        var padded = new Uint8Array(WSPR_MESSAGE_SIZE);
        padded.set(data.subarray ? data.subarray(0, data.length) : data, 0);

        var output = new Uint8Array(WSPR_BIT_COUNT);
        var reg0 = 0 >>> 0;
        var reg1 = 0 >>> 0;
        var bitIndex = 0;

        for (var i = 0; i < WSPR_MESSAGE_SIZE; i++) {
            for (var j = 0; j < 8; j++) {
                var inputBit = (((padded[i] << j) & 0x80) === 0x80) ? 1 : 0;

                reg0 = (((reg0 << 1) | inputBit) >>> 0);
                reg1 = (((reg1 << 1) | inputBit) >>> 0);

                var regTemp = (reg0 & 0xf2d05351) >>> 0;
                var parityBit = 0;
                for (var k = 0; k < 32; k++) {
                    parityBit ^= (regTemp & 0x01);
                    regTemp >>>= 1;
                }
                output[bitIndex++] = parityBit;

                regTemp = (reg1 & 0xe4613c47) >>> 0;
                parityBit = 0;
                for (var m = 0; m < 32; m++) {
                    parityBit ^= (regTemp & 0x01);
                    regTemp >>>= 1;
                }
                output[bitIndex++] = parityBit;

                if (bitIndex >= WSPR_BIT_COUNT) break;
            }
        }

        return output;
    }

    function interleave(data) {
        var out = new Uint8Array(WSPR_BIT_COUNT);
        var i = 0;

        for (var j = 0; j < 255; j++) {
            var j2 = j;
            var rev = 0;
            for (var k = 0; k < 8; k++) {
                if (j2 & 0x01) rev |= (1 << (7 - k));
                j2 >>>= 1;
            }
            if (rev < WSPR_BIT_COUNT) {
                out[rev] = data[i];
                i++;
            }
            if (i >= WSPR_BIT_COUNT) break;
        }

        return out;
    }

    function integrateSyncValues(data) {
        if (!data || data.length !== SYNC_VECTOR.length) {
            throw new Error('input data must be the same size as the sync array (' + SYNC_VECTOR.length + ' elements)');
        }
        var out = new Uint8Array(WSPR_BIT_COUNT);
        for (var i = 0; i < WSPR_BIT_COUNT; i++) {
            out[i] = data[i] * 2 + SYNC_VECTOR[i];
        }
        return out;
    }

    function encodeSymbols(callsign, location, power) {
        return integrateSyncValues(interleave(convolve(getMessageBytes(callsign, location, power))));
    }

    function applyRaisedCosineEnvelope(samples, sampleRate, rampMs) {
        var rampSamples = Math.max(1, Math.round(sampleRate * (rampMs / 1000)));
        rampSamples = Math.min(rampSamples, Math.floor(samples.length / 2));
        for (var i = 0; i < rampSamples; i++) {
            var gain = 0.5 - 0.5 * Math.cos(Math.PI * (i + 1) / rampSamples);
            samples[i] *= gain;
            samples[samples.length - 1 - i] *= gain;
        }
    }

    function generateSamplesFromLevels(levels, options) {
        options = options || {};
        var sampleRate = options.sampleRate || WSPR_SAMPLE_RATE;
        if (sampleRate !== WSPR_SAMPLE_RATE) throw new Error('WSPR TX generator currently requires a 12 kHz sample rate');

        var baseFrequency = Number(options.baseFrequency);
        if (!isFinite(baseFrequency)) baseFrequency = 1500;
        var amplitude = Number(options.amplitude);
        if (!isFinite(amplitude)) amplitude = 0.85;
        var rampMs = Number(options.rampMs);
        if (!isFinite(rampMs)) rampMs = 10;

        var samples = new Float32Array(levels.length * WSPR_SYMBOL_SAMPLES);
        var phase = 0;
        var idx = 0;

        for (var i = 0; i < levels.length; i++) {
            var tone = baseFrequency + levels[i] * WSPR_TONE_SPACING;
            var phaseStep = 2 * Math.PI * tone / sampleRate;
            for (var j = 0; j < WSPR_SYMBOL_SAMPLES; j++) {
                samples[idx++] = Math.sin(phase) * amplitude;
                phase += phaseStep;
                if (phase > Math.PI * 4096) phase -= Math.PI * 4096;
            }
        }

        applyRaisedCosineEnvelope(samples, sampleRate, rampMs);
        return samples;
    }

    function generateSamples(callsign, location, power, options) {
        return generateSamplesFromLevels(encodeSymbols(callsign, location, power), options);
    }

    function buildMessageText(callsign, location, power) {
        return sanitizeCallsign(callsign).trim() + ' ' + sanitizeLocation(location) + ' ' + sanitizePower(power);
    }

    global.wsprEncodeLib = {
        WSPR_BIT_COUNT: WSPR_BIT_COUNT,
        WSPR_SYMBOL_COUNT: WSPR_SYMBOL_COUNT,
        WSPR_SAMPLE_RATE: WSPR_SAMPLE_RATE,
        WSPR_SYMBOL_SAMPLES: WSPR_SYMBOL_SAMPLES,
        WSPR_TONE_SPACING: WSPR_TONE_SPACING,
        getValidPowerLevels: function() { return VALID_POWER_LEVELS.slice(); },
        getPowerDescription: getPowerDescription,
        sanitizeCallsign: sanitizeCallsign,
        sanitizeLocation: sanitizeLocation,
        sanitizePower: sanitizePower,
        getMessageBytes: getMessageBytes,
        convolve: convolve,
        interleave: interleave,
        integrateSyncValues: integrateSyncValues,
        encodeSymbols: encodeSymbols,
        generateSamplesFromLevels: generateSamplesFromLevels,
        generateSamples: generateSamples,
        buildMessageText: buildMessageText
    };
})(window);
