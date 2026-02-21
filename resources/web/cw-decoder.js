// CW Decoder for wfview - Based on web-deep-cw-decoder
// This implementation copies the demo as closely as possible

(function() {
    'use strict';

    // Constants from the demo
    const FFT_SIZE = 4096;  // 2^12
    const MIN_FREQ_HZ = 100;
    const MAX_FREQ_HZ = 1500;
    const DECODABLE_MIN_FREQ_HZ = 400;
    const DECODABLE_MAX_FREQ_HZ = 1200;
    const BUFFER_DURATION_S = 12;
    const INFERENCE_INTERVAL_MS = 800;  // Even slower updates for readability
    const SAMPLE_RATE = 3200;
    const BUFFER_SAMPLES = BUFFER_DURATION_S * SAMPLE_RATE;  // 38400

    // Decoder state
    const decoderState = {
        enabled: false,
        loading: false,
        loaded: false,
        gain: 0,
        filterWidth: 250,
        filterFreq: null,
        currentSegments: [],
    };

    // Audio nodes
    let analyserNode = null;
    let gainNode = null;
    let processorNode = null;
    let audioContext = null;
    let decoderWorker = null;
    let rafId = null;

    // Audio buffer for inference - sliding window like demo
    const audioBuffer = new Float32Array(BUFFER_SAMPLES);

    // Canvas
    let canvas = null;
    let ctx2d = null;
    let column = null;
    let renderState = { lastTime: performance.now(), pixelAccumulator: 0 };

    // Color LUT
    let colorLUT = null;

    // Initialize
    function init() {
        console.log('[CW Decoder] Initializing...');
        colorLUT = buildColorLUT();
        createUI();
    }

    // HSL to RGB conversion (from demo)
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

    // Build color LUT (from demo)
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

    // Create UI
    function createUI() {
        const cwBar = document.getElementById('cwBar');
        if (!cwBar) {
            setTimeout(createUI, 500);
            return;
        }

        if (document.getElementById('cwDecoderToggle')) return;

        // Add DECODE button to the cw-bar-header (next to CW label)
        const header = cwBar.querySelector('.cw-bar-header');
        if (header) {
            const decodeBtn = document.createElement('button');
            decodeBtn.id = 'cwDecoderToggle';
            decodeBtn.className = 'cw-decoder-btn';
            decodeBtn.textContent = 'DECODE';
            decodeBtn.style.marginLeft = '10px';
            decodeBtn.style.padding = '2px 8px';
            decodeBtn.style.background = '#001a00';
            decodeBtn.style.border = '1px solid #0a0';
            decodeBtn.style.color = '#0a0';
            decodeBtn.style.fontSize = '10px';
            decodeBtn.style.fontWeight = 'bold';
            decodeBtn.style.cursor = 'pointer';
            decodeBtn.style.fontFamily = 'monospace';

            const cwLabel = header.querySelector('.cw-label');
            if (cwLabel) {
                cwLabel.parentNode.insertBefore(decodeBtn, cwLabel.nextSibling);
            } else {
                header.insertBefore(decodeBtn, header.firstChild);
            }
        }

        // Create decoder content section (waterfall + text) - initially hidden
        const section = document.createElement('div');
        section.id = 'cwDecoderSection';
        section.style.display = 'none';  // Hidden when decode is off
        section.innerHTML = `
            <div class="cw-scope-container">
                <canvas id="cwScopeCanvas"></canvas>
                <div id="cwFilterBand"></div>
            </div>
            <div id="cwDecoderText"></div>
        `;

        // Insert decoder section BEFORE the macro grids (transmit section)
        const firstChild = cwBar.firstChild;
        cwBar.insertBefore(section, firstChild);

        canvas = document.getElementById('cwScopeCanvas');
        if (canvas) {
            ctx2d = canvas.getContext('2d');
            setupCanvasSizing();
        }

        addStyles();
        setupEventListeners();

        console.log('[CW Decoder] UI created');
    }

    function addStyles() {
        if (document.getElementById('cwDecoderStyles')) return;

        const style = document.createElement('style');
        style.id = 'cwDecoderStyles';
        style.textContent = `
            #cwDecoderSection { margin: 8px 0; border-bottom: 1px solid #0a0; padding-bottom: 8px; }
            #cwDecoderToggle { }
            #cwDecoderToggle:hover { background: #0a0 !important; color: #000 !important; }
            #cwDecoderToggle.active { background: #0a0 !important; color: #000 !important; }
            #cwDecoderToggle.loading { background: #1a1a00 !important; border-color: #aa0 !important; color: #aa0 !important; }
            .cw-scope-container { position: relative; width: 100%; height: 100px; }
            #cwScopeCanvas { display: block; background: #000; width: 100%; height: 100px; border-radius: 4px; border: 1px solid #0a0; }
            #cwFilterBand { position: absolute; left: 0; right: 0; pointer-events: none; border-top: 1px solid #f00; border-bottom: 1px solid #f00; display: none; }
            #cwFilterBand.active { display: block; }
            #cwDecoderText { width: 100%; font-size: 20px; background: #000; border-radius: 4px; border: 1px solid #0a0; height: 32px; margin-top: 8px; color: #0f0; overflow: hidden; box-sizing: border-box; font-family: 'Courier New', monospace; line-height: 32px; display: flex; justify-content: space-between; align-items: center; padding: 0 8px; }
        `;
        document.head.appendChild(style);
    }

    function setupCanvasSizing() {
        if (!canvas) return;
        const rect = canvas.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        canvas.width = (rect.width || canvas.clientWidth || 300) * dpr;
        canvas.height = (rect.height || canvas.clientHeight || 100) * dpr;
        renderState.pixelAccumulator = 0;
    }

    function setupEventListeners() {
        document.getElementById('cwDecoderToggle')?.addEventListener('click', toggleDecoder);

        if (canvas) {
            canvas.addEventListener('click', handleCanvasClick);
            canvas.addEventListener('wheel', handleWheel, { passive: false });
        }

        const resizeObserver = new ResizeObserver(() => setupCanvasSizing());
        if (canvas) resizeObserver.observe(canvas);
    }

    // Toggle decoder on/off
    async function toggleDecoder() {
        if (decoderState.loading) return;
        if (decoderState.enabled) {
            stopDecoder();
        } else {
            await startDecoder();
        }
    }

    async function startDecoder() {
        if (decoderState.enabled) return;

        decoderState.loading = true;
        updateButtons();

        try {
            // Wait for wfview's audio context to be available (poll for up to 5 seconds)
            let waitCount = 0;
            while (!window.audioCtx && waitCount < 50) {
                await new Promise(r => setTimeout(r, 100));
                waitCount++;
            }

            audioContext = window.audioCtx;
            if (!audioContext) {
                console.error('[CW Decoder] No audio context available. Make sure audio is enabled.');
                decoderState.loading = false;
                updateButtons();
                return;
            }

            // Create audio nodes (matching demo)
            analyserNode = audioContext.createAnalyser();
            analyserNode.fftSize = FFT_SIZE;
            analyserNode.smoothingTimeConstant = 0;
            analyserNode.minDecibels = -70;
            analyserNode.maxDecibels = -30;

            gainNode = audioContext.createGain();
            gainNode.gain.value = Math.pow(10, decoderState.gain / 20);

            // ScriptProcessor for audio capture (like demo's useAudioProcessing)
            processorNode = audioContext.createScriptProcessor(2048, 1, 1);
            processorNode.onaudioprocess = (e) => {
                const inputData = e.inputBuffer.getChannelData(0);
                const outputData = e.outputBuffer.getChannelData(0);
                outputData.set(inputData);  // Pass-through

                if (!decoderState.enabled) return;

                // Sliding window: copyWithin + set (exactly like demo)
                const chunkLen = inputData.length;
                const sourceRate = audioContext.sampleRate;  // 48000
                const decimationFactor = Math.round(sourceRate / SAMPLE_RATE);  // 15
                const samplesToProduce = Math.floor(chunkLen / decimationFactor);

                // Simple decimation (no gain here - gain is applied via gainNode)
                const resampledChunk = new Float32Array(samplesToProduce);

                for (let i = 0; i < samplesToProduce; i++) {
                    let sum = 0;
                    const startIdx = i * decimationFactor;
                    for (let j = 0; j < decimationFactor; j++) {
                        sum += inputData[startIdx + j];
                    }
                    resampledChunk[i] = sum / decimationFactor;
                }

                // Sliding window (exactly like demo)
                const resampledLen = resampledChunk.length;
                audioBuffer.copyWithin(0, resampledLen);
                audioBuffer.set(resampledChunk, BUFFER_SAMPLES - resampledLen);
            };

            // Connect nodes
            connectAudioNodes();

            // Start worker
            decoderWorker = new Worker('cw-decoder-worker.js');
            decoderWorker.onmessage = handleWorkerMessage;
            decoderWorker.postMessage({ type: 'loadModel' });

            // Wait for model
            await new Promise((resolve, reject) => {
                const timeout = setTimeout(() => reject(new Error('Timeout')), 30000);
                const check = setInterval(() => {
                    if (decoderState.loaded) {
                        clearInterval(check);
                        clearTimeout(timeout);
                        resolve();
                    }
                }, 100);
            });

            decoderState.enabled = true;
            setupCanvasSizing();
            startRendering();
            updateFilterBand();

        } catch (err) {
            console.error('[CW Decoder] Failed to start:', err);
            stopDecoder();
        } finally {
            decoderState.loading = false;
            updateButtons();
        }
    }

    function connectAudioNodes() {
        if (window.audioWorkletNode && window.audioGainNode) {
            window.audioWorkletNode.disconnect();
            window.audioWorkletNode.connect(gainNode);
            gainNode.connect(processorNode);
            processorNode.connect(analyserNode);
            analyserNode.connect(window.audioGainNode);
        } else if (window.audioScriptNode && window.audioGainNode) {
            window.audioScriptNode.disconnect();
            window.audioScriptNode.connect(gainNode);
            gainNode.connect(processorNode);
            processorNode.connect(analyserNode);
            analyserNode.connect(window.audioGainNode);
        }
    }

    function disconnectAudioNodes() {
        if (window.audioGainNode) {
            if (window.audioWorkletNode) {
                processorNode?.disconnect();
                gainNode?.disconnect();
                analyserNode?.disconnect();
                window.audioWorkletNode.disconnect();
                window.audioWorkletNode.connect(window.audioGainNode);
            } else if (window.audioScriptNode) {
                processorNode?.disconnect();
                gainNode?.disconnect();
                analyserNode?.disconnect();
                window.audioScriptNode.disconnect();
                window.audioScriptNode.connect(window.audioGainNode);
            }
        }
    }

    function stopDecoder() {
        if (rafId) {
            cancelAnimationFrame(rafId);
            rafId = null;
        }

        if (processorNode) {
            processorNode.disconnect();
            processorNode.onaudioprocess = null;
            processorNode = null;
        }

        disconnectAudioNodes();

        if (decoderWorker) {
            decoderWorker.terminate();
            decoderWorker = null;
        }

        decoderState.enabled = false;
        decoderState.loaded = false;
        decoderState.loading = false;
        decoderState.currentSegments = [];
        updateText();
        updateButtons();
    }

    function handleWorkerMessage(event) {
        const msg = event.data;
        switch (msg.type) {
            case 'modelLoaded':
                decoderState.loaded = true;
                break;
            case 'inferenceResult':
                decoderState.currentSegments = msg.segments || [];
                updateText();
                break;
            case 'error':
                console.error('[CW Decoder Worker] Error:', msg.error);
                break;
        }
    }

    // Spectrogram rendering (exactly like demo)
    function startRendering() {
        if (!canvas || !ctx2d || !analyserNode) return;

        const freqBins = analyserNode.frequencyBinCount;
        const dataArray = new Uint8Array(freqBins);
        renderState.lastTime = performance.now();
        renderState.pixelAccumulator = 0;

        const render = () => {
            if (!decoderState.enabled || !canvas || !analyserNode) return;

            const now = performance.now();
            const dt = (now - renderState.lastTime) / 1000;
            renderState.lastTime = now;

            const pxPerSec = canvas.width / BUFFER_DURATION_S;
            renderState.pixelAccumulator += dt * pxPerSec;

            let step = Math.floor(renderState.pixelAccumulator);
            if (step <= 0) {
                rafId = requestAnimationFrame(render);
                return;
            }
            renderState.pixelAccumulator -= step;
            if (step > canvas.width) step = canvas.width;

            analyserNode.getByteFrequencyData(dataArray);

            // Scroll left (exactly like demo)
            ctx2d.drawImage(
                canvas,
                0, 0, canvas.width, canvas.height,
                -step, 0, canvas.width, canvas.height
            );

            if (!column || column.height !== canvas.height) {
                column = ctx2d.createImageData(1, canvas.height);
            }

            const buf = column.data;
            const nyquist = audioContext.sampleRate / 2;
            const minBin = Math.floor((MIN_FREQ_HZ / nyquist) * (freqBins - 1));
            const maxBin = Math.min(
                freqBins - 1,
                Math.floor((Math.min(MAX_FREQ_HZ, nyquist) / nyquist) * (freqBins - 1))
            );

            for (let y = 0; y < canvas.height; y++) {
                const invY = canvas.height - 1 - y;
                const binRange = maxBin - minBin;
                const idx = minBin + Math.floor((invY / Math.max(1, canvas.height - 1)) * binRange);
                const v = dataArray[idx];
                const [r, g, b] = colorLUT[v];
                const p = y * 4;
                buf[p] = r;
                buf[p + 1] = g;
                buf[p + 2] = b;
                buf[p + 3] = 255;
            }

            for (let i = 0; i < step; i++) {
                ctx2d.putImageData(column, canvas.width - step + i, 0);
            }

            // Run inference periodically
            if (now - (renderState.lastInferenceTime || 0) > INFERENCE_INTERVAL_MS) {
                runInference();
                renderState.lastInferenceTime = now;
            }

            rafId = requestAnimationFrame(render);
        };

        renderState.lastInferenceTime = performance.now();
        rafId = requestAnimationFrame(render);
    }

    function runInference() {
        if (!decoderState.enabled || !decoderWorker || !decoderState.loaded) return;

        const audioCopy = audioBuffer.slice();
        decoderWorker.postMessage({
            type: 'runInference',
            audioBuffer: audioCopy,
            filterFreq: decoderState.filterFreq,
            filterWidth: decoderState.filterWidth
        }, [audioCopy.buffer]);
    }

    // Text display - flexbox with proper spacing
    function updateText() {
        const container = document.getElementById('cwDecoderText');
        if (!container) return;

        const segments = decoderState.currentSegments;

        // Build character divs with flex layout
        let html = '';
        let lastWasSpace = false;

        segments.forEach((segment) => {
            const chars = Array.from(segment.text);
            chars.forEach((char) => {
                if (char === ' ') {
                    // Use flex-grow for spaces, but collapse consecutive ones
                    if (!lastWasSpace) {
                        html += '<div style="flex: 1;"></div>';
                        lastWasSpace = true;
                    }
                } else {
                    const color = segment.isAbbreviation ? '#ff0' : '#0f0';
                    html += `<div style="color: ${color}; font-weight: ${segment.isAbbreviation ? 'bold' : 'normal'}; flex-shrink: 0;">${char}</div>`;
                    lastWasSpace = false;
                }
            });
        });

        container.innerHTML = html || '<div></div>';
    }

    function updateButtons() {
        const toggleBtn = document.getElementById('cwDecoderToggle');
        const section = document.getElementById('cwDecoderSection');

        if (toggleBtn) {
            if (decoderState.loading) {
                toggleBtn.textContent = 'LOADING...';
                toggleBtn.className = 'loading';
            } else if (decoderState.enabled) {
                toggleBtn.textContent = 'DECODE';
                toggleBtn.className = 'active';
            } else {
                toggleBtn.textContent = 'DECODE';
                toggleBtn.className = '';
            }
        }

        // Show/hide decoder section based on state
        if (section) {
            section.style.display = decoderState.enabled ? 'block' : 'none';
        }
    }

    // Filter band display
    function updateFilterBand() {
        const band = document.getElementById('cwFilterBand');
        if (!band) return;

        if (decoderState.filterFreq === null) {
            band.classList.remove('active');
            return;
        }

        const range = MAX_FREQ_HZ - MIN_FREQ_HZ;
        const half = decoderState.filterWidth / 2;
        const lower = Math.max(MIN_FREQ_HZ, decoderState.filterFreq - half);
        const upper = Math.min(MAX_FREQ_HZ, decoderState.filterFreq + half);

        const topPercent = ((MAX_FREQ_HZ - upper) / range) * 100;
        const heightPercent = ((upper - lower) / range) * 100;

        band.style.top = `${topPercent}%`;
        band.style.height = `${heightPercent}%`;
        band.classList.add('active');
    }

    // Canvas interactions (like demo)
    function constrainFrequency(freq) {
        const half = decoderState.filterWidth / 2;
        if (freq - half < DECODABLE_MIN_FREQ_HZ) return DECODABLE_MIN_FREQ_HZ + half;
        if (freq + half > DECODABLE_MAX_FREQ_HZ) return DECODABLE_MAX_FREQ_HZ - half;
        return freq;
    }

    function handleCanvasClick(event) {
        // Click to set frequency - filter stays on
        const rect = canvas.getBoundingClientRect();
        const y = event.clientY - rect.top;
        const invY = rect.height - y;
        const freqRange = MAX_FREQ_HZ - MIN_FREQ_HZ;
        const rawFreq = MIN_FREQ_HZ + (invY / rect.height) * freqRange;
        decoderState.filterFreq = Math.ceil(constrainFrequency(rawFreq));
        updateFilterBand();
    }

    function handleWheel(e) {
        e.preventDefault();
        if (decoderState.filterFreq === null) return;

        const step = 20;
        let newFreq = decoderState.filterFreq;

        if (e.deltaY < 0) {
            newFreq += step;
        } else if (e.deltaY > 0) {
            newFreq -= step;
        }

        decoderState.filterFreq = Math.round(constrainFrequency(newFreq));
        updateFilterBand();
    }

    // Expose API
    window.CWDecoder = {
        init: init,
        get state() { return decoderState; }
    };

    // Auto-init
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
