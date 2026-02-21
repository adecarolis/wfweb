// FFT and STFT implementation for CW decoder
// Ported from web-deep-cw-decoder/src/stft.ts

class FFT {
    constructor(fftSize) {
        if ((fftSize & (fftSize - 1)) !== 0) {
            throw new Error("FFT size must be a power of 2.");
        }
        this.fftSize = fftSize;
        this.reverseTable = new Uint32Array(fftSize);
        this.sinTable = new Float32Array(fftSize);
        this.cosTable = new Float32Array(fftSize);

        let limit = 1;
        let bit = fftSize >> 1;
        while (limit < fftSize) {
            for (let i = 0; i < limit; i++) {
                this.reverseTable[i + limit] = this.reverseTable[i] + bit;
            }
            limit = limit << 1;
            bit = bit >> 1;
        }

        for (let i = 0; i < fftSize; i++) {
            const angle = (-2 * Math.PI * i) / fftSize;
            this.sinTable[i] = Math.sin(angle);
            this.cosTable[i] = Math.cos(angle);
        }
    }

    transform(complexArray) {
        for (let i = 0; i < this.fftSize; i++) {
            const reversedIndex = this.reverseTable[i];
            if (i < reversedIndex) {
                const tmpReal = complexArray[i * 2];
                const tmpImag = complexArray[i * 2 + 1];
                complexArray[i * 2] = complexArray[reversedIndex * 2];
                complexArray[i * 2 + 1] = complexArray[reversedIndex * 2 + 1];
                complexArray[reversedIndex * 2] = tmpReal;
                complexArray[reversedIndex * 2 + 1] = tmpImag;
            }
        }

        for (let halfSize = 1; halfSize < this.fftSize; halfSize *= 2) {
            const step = 2 * halfSize;
            const angleStep = this.fftSize / step;
            for (let i = 0; i < this.fftSize; i += step) {
                for (let j = 0; j < halfSize; j++) {
                    const angleIndex = j * angleStep;
                    const wReal = this.cosTable[angleIndex];
                    const wImag = this.sinTable[angleIndex];

                    const i_j = (i + j) * 2;
                    const i_j_half = (i + j + halfSize) * 2;

                    const tr = wReal * complexArray[i_j_half] - wImag * complexArray[i_j_half + 1];
                    const ti = wReal * complexArray[i_j_half + 1] + wImag * complexArray[i_j_half];

                    const ur = complexArray[i_j];
                    const ui = complexArray[i_j + 1];

                    complexArray[i_j] = ur + tr;
                    complexArray[i_j + 1] = ui + ti;
                    complexArray[i_j_half] = ur - tr;
                    complexArray[i_j_half + 1] = ui - ti;
                }
            }
        }
    }
}

class STFT {
    constructor(fftSize, hopSize) {
        if (fftSize <= 0) throw new Error("FFT size must be positive.");
        if (hopSize <= 0) throw new Error("Hop size must be positive.");
        this.fftSize = fftSize;
        this.hopSize = hopSize;
        this.fft = new FFT(fftSize);
        this.window = this.generateHanningWindow();
    }

    generateHanningWindow() {
        const window = new Float32Array(this.fftSize);
        for (let i = 0; i < this.fftSize; i++) {
            window[i] = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (this.fftSize - 1)));
        }
        return window;
    }

    analyze(signal) {
        const spectrogram = [];
        const frame = new Float32Array(this.fftSize);
        const complexFrame = new Float32Array(this.fftSize * 2);

        for (let i = 0; i + this.fftSize <= signal.length; i += this.hopSize) {
            const signalSlice = signal.subarray(i, i + this.fftSize);
            frame.set(signalSlice);

            for (let j = 0; j < this.fftSize; j++) {
                frame[j] *= this.window[j];
            }

            for (let j = 0; j < this.fftSize; j++) {
                complexFrame[j * 2] = frame[j];
                complexFrame[j * 2 + 1] = 0;
            }

            this.fft.transform(complexFrame);
            spectrogram.push(complexFrame.slice());
        }

        return spectrogram;
    }
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { FFT, STFT };
}
