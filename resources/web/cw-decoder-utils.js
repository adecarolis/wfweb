// Utility functions for CW decoder
// Ported from web-deep-cw-decoder utils

// Color utilities
function hslToRgb(h, s, l) {
    const hue2rgb = (p, q, t) => {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1 / 6) return p + (q - p) * 6 * t;
        if (t < 1 / 2) return q;
        if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
        return p;
    };

    let r, g, b;

    if (s === 0) {
        r = g = b = l;
    } else {
        const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        const p = 2 * l - q;
        r = hue2rgb(p, q, h + 1 / 3);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1 / 3);
    }

    return [Math.round(r * 255), Math.round(g * 255), Math.round(b * 255)];
}

function buildColorLUT() {
    const lut = new Array(256);

    for (let v = 0; v < 256; v++) {
        let t = v / 255;
        const gamma = 2.2;
        t = Math.pow(t, gamma);

        const hue = (220 * (1 - t)) / 360;
        const sat = 1.0;
        const light = 0.15 + 0.75 * t;

        lut[v] = hslToRgb(hue, sat, light);
    }

    return lut;
}

// Frequency utilities
const MIN_FREQ_HZ = 100;
const MAX_FREQ_HZ = 1500;
const DECODABLE_MIN_FREQ_HZ = 400;
const DECODABLE_MAX_FREQ_HZ = 1200;

function calculateBandPosition(filterFreq, filterWidth) {
    const isEnableFilter = filterFreq != null;
    const _filterFreq = isEnableFilter ? filterFreq : 800;
    const _filterWidth = isEnableFilter ? filterWidth : 800;

    const range = MAX_FREQ_HZ - MIN_FREQ_HZ;
    const half = _filterWidth / 2;
    const lower = Math.max(MIN_FREQ_HZ, _filterFreq - half);
    const upper = Math.min(MAX_FREQ_HZ, _filterFreq + half);

    const topPercent = ((MAX_FREQ_HZ - upper) / range) * 100;
    const heightPercent = ((upper - lower) / range) * 100;

    return { topPercent, heightPercent };
}

function constrainFrequency(frequency, filterWidth) {
    const halfWidth = filterWidth / 2;

    if (frequency - halfWidth < DECODABLE_MIN_FREQ_HZ) {
        return DECODABLE_MIN_FREQ_HZ + halfWidth;
    }

    if (frequency + halfWidth > DECODABLE_MAX_FREQ_HZ) {
        return DECODABLE_MAX_FREQ_HZ - halfWidth;
    }

    return frequency;
}

function calculateFrequencyFromY(y, canvasHeight, filterWidth) {
    const invY = canvasHeight - y;
    const frequencyRange = MAX_FREQ_HZ - MIN_FREQ_HZ;
    const rawFrequency = MIN_FREQ_HZ + (invY / canvasHeight) * frequencyRange;

    return constrainFrequency(rawFrequency, filterWidth);
}

// Audio filter utilities (4th order bandpass)
function applyBandpassFilter(audio, sampleRate, centerFreq, bandwidth) {
    const lowFreq = centerFreq - bandwidth / 2;
    const highFreq = centerFreq + bandwidth / 2;

    // Simple FIR bandpass approximation
    const filterOrder = 64;
    const filtered = new Float32Array(audio.length);

    // Normalize frequencies
    const omegaLow = (2 * Math.PI * lowFreq) / sampleRate;
    const omegaHigh = (2 * Math.PI * highFreq) / sampleRate;

    for (let i = 0; i < audio.length; i++) {
        let sum = 0;
        let weightSum = 0;

        for (let j = -filterOrder / 2; j < filterOrder / 2; j++) {
            const idx = i + j;
            if (idx >= 0 && idx < audio.length) {
                // Window function (Hamming)
                const window = 0.54 - 0.46 * Math.cos((2 * Math.PI * (j + filterOrder / 2)) / filterOrder);
                // Ideal bandpass kernel
                const t = j;
                let kernel = 0;
                if (t !== 0) {
                    kernel = (Math.sin(omegaHigh * t) - Math.sin(omegaLow * t)) / (Math.PI * t);
                } else {
                    kernel = (omegaHigh - omegaLow) / Math.PI;
                }

                sum += audio[idx] * kernel * window;
                weightSum += kernel * window;
            }
        }

        filtered[i] = weightSum > 0 ? sum / weightSum : 0;
    }

    return filtered;
}

// Spectrogram utilities
const FFT_LENGTH = 256;
const HOP_LENGTH = 64;
const SAMPLE_RATE = 3200;
const BUFFER_DURATION_S = 12;
const BUFFER_SAMPLES = BUFFER_DURATION_S * SAMPLE_RATE;

function computeMagnitudeSpectrogram(complexSpectrogram) {
    return complexSpectrogram.map((complexFrame) => {
        const magnitudes = new Float32Array(FFT_LENGTH / 2 + 1);

        for (let j = 0; j < magnitudes.length; j++) {
            const real = complexFrame[j * 2];
            const imag = complexFrame[j * 2 + 1];
            magnitudes[j] = Math.sqrt(real * real + imag * imag);
        }

        return magnitudes;
    });
}

function cropSpectrogram(spectrogram) {
    const nBins = FFT_LENGTH / 2 + 1;
    const cutBins = Math.floor(nBins / 4);
    const startBin = cutBins;
    const endBin = nBins - cutBins;

    return spectrogram.map((frame) => frame.slice(startBin, endBin));
}

function audioToSpectrogram(audio, filterFreq, filterWidth) {
    let processedAudio = audio;
    if (filterFreq !== null && filterWidth > 0) {
        processedAudio = applyBandpassFilter(audio, SAMPLE_RATE, filterFreq, filterWidth);
    }

    const stft = new STFT(FFT_LENGTH, HOP_LENGTH);
    const complexSpectrogram = stft.analyze(processedAudio);
    const magnitudeSpectrogram = computeMagnitudeSpectrogram(complexSpectrogram);

    return cropSpectrogram(magnitudeSpectrogram);
}

// Text decoder utilities
const ENGLISH_VOCABULARY = [
    "[UNK]", "/", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "?",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "\ue030", "\ue031", "\ue032", "\ue033", "\ue034", "\ue035", "\ue036", " "
];

const ABBREVIATION_MAP = {
    "\ue030": "AR",
    "\ue031": "BT",
    "\ue032": "HH",
    "\ue033": "KN",
    "\ue034": "SK",
    "\ue035": "BK",
    "\ue036": "UR"
};

function replaceConsecutiveChars(str) {
    const regex = /(\S)\1+/g;
    return str.replace(regex, (match, p1) => {
        return p1 + " ".repeat(match.length - 1);
    });
}

function convertAbbreviationsWithSegments(str) {
    const abbreviations = Object.entries(ABBREVIATION_MAP);

    if (abbreviations.length === 0) {
        return [{ text: str, isAbbreviation: false }];
    }

    const abbrevPattern = abbreviations.map(([abbrev]) => abbrev).join("|");
    const regex = new RegExp(`(${abbrevPattern})`, "g");

    const segments = [];
    let lastIndex = 0;
    let match;

    while ((match = regex.exec(str)) !== null) {
        if (match.index > lastIndex) {
            segments.push({
                text: str.slice(lastIndex, match.index),
                isAbbreviation: false
            });
        }

        const matchedText = match[0];
        const abbrevEntry = abbreviations.find(([abbrev]) => abbrev === matchedText);
        const expansion = abbrevEntry ? abbrevEntry[1] : matchedText;

        segments.push({
            text: expansion,
            isAbbreviation: true
        });

        lastIndex = regex.lastIndex;
    }

    if (lastIndex < str.length) {
        segments.push({
            text: str.slice(lastIndex),
            isAbbreviation: false
        });
    }

    return segments.length > 0 ? segments : [{ text: str, isAbbreviation: false }];
}

function decodePredictions(pred, predShape) {
    const [batchSize, timeSteps, numClasses] = predShape;
    const outputSegments = [];

    for (let i = 0; i < batchSize; i++) {
        const predIndices = [];
        for (let t = 0; t < timeSteps; t++) {
            let maxProb = -Infinity;
            let maxIndex = 0;
            const offset = i * timeSteps * numClasses + t * numClasses;

            for (let c = 0; c < numClasses; c++) {
                if (pred[offset + c] > maxProb) {
                    maxProb = pred[offset + c];
                    maxIndex = c;
                }
            }
            predIndices.push(maxIndex);
        }

        const resText = predIndices.map((c) => ENGLISH_VOCABULARY[c] || "").join("");
        const processedSegments = convertAbbreviationsWithSegments(
            replaceConsecutiveChars(resText)
        );

        outputSegments.push(processedSegments);
    }

    return outputSegments;
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        buildColorLUT,
        calculateBandPosition,
        constrainFrequency,
        calculateFrequencyFromY,
        applyBandpassFilter,
        audioToSpectrogram,
        decodePredictions,
        MIN_FREQ_HZ,
        MAX_FREQ_HZ,
        DECODABLE_MIN_FREQ_HZ,
        DECODABLE_MAX_FREQ_HZ,
        FFT_LENGTH,
        HOP_LENGTH,
        SAMPLE_RATE,
        BUFFER_DURATION_S,
        BUFFER_SAMPLES
    };
}
