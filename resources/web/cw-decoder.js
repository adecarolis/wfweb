// CW Decoder Controller for wfview
// Uses Web Audio API AnalyserNode for proper spectrum display

(function() {
    'use strict';

    // Decoder state
    let decoderState = {
        enabled: false,
        loading: false,
        loaded: false,
        gain: 0,           // 0 or 20 dB
        filterWidth: 250,  // 100 or 250 Hz
        filterFreq: 600,   // Center frequency (null = disabled)
        currentSegments: [],
        isDecoding: false
    };

    // Web Audio nodes
    let analyserNode = null;
    let gainNode = null;
    let audioContext = null;
    let sourceNode = null;

    // Worker
    let decoderWorker = null;

    // Waterfall rendering
    let waterfallCanvas = null;
    let waterfallCtx = null;
    let rafId = null;
    let colorLUT = null;
    let frequencyData = null;
    let renderState = { lastTime: performance.now(), pixelAccumulator: 0 };

    // Constants
    const FFT_SIZE = 4096;  // Higher resolution FFT
    const MIN_FREQ = 100;   // Hz - bottom of display
    const MAX_FREQ = 1500;  // Hz - top of display
    const BUFFER_DURATION_S = 12;
    const INFERENCE_INTERVAL_MS = 250;

    // Audio buffer for inference (12 seconds at target sample rate)
    let audioBuffer = [];
    let lastInferenceTime = 0;

    // Initialize decoder
    function initDecoder() {
        colorLUT = buildColorLUT();
        createDecoderUI();
        watchCWBarVisibility();
    }

    // Build color lookup table
    function buildColorLUT() {
        const lut = new Array(256);
        for (let v = 0; v < 256; v++) {
            const t = v / 255;
            let r, g, b;

            if (t < 0.25) {
                const tt = t / 0.25;
                r = 0;
                g = 0;
                b = Math.floor(tt * 64);
            } else if (t < 0.5) {
                const tt = (t - 0.25) / 0.25;
                r = 0;
                g = Math.floor(tt * 255);
                b = 64 + Math.floor(tt * 191);
            } else if (t < 0.75) {
                const tt = (t - 0.5) / 0.25;
                r = Math.floor(tt * 255);
                g = 255;
                b = 0;
            } else {
                const tt = (t - 0.75) / 0.25;
                r = 255;
                g = Math.floor((1 - tt) * 255);
                b = 0;
            }
            lut[v] = [r, g, b];
        }
        return lut;
    }

    // Create decoder UI elements
    function createDecoderUI() {
        const cwBar = document.getElementById('cwBar');
        if (!cwBar) {
            setTimeout(createDecoderUI, 500);
            return;
        }

        if (document.getElementById('cwDecoderSection')) return;

        const macroGrid2 = document.getElementById('cwMacroGrid2');
        if (!macroGrid2) return;

        const decoderSection = document.createElement('div');
        decoderSection.id = 'cwDecoderSection';
        decoderSection.className = 'cw-decoder-section';
        decoderSection.innerHTML = `
            <div class="cw-decoder-controls">
                <button id="cwDecoderToggle" class="cw-decoder-btn">DECODE: OFF</button>
                <button id="cwDecoderGain" class="cw-decoder-btn">GAIN: 0dB</button>
                <button id="cwDecoderFilter" class="cw-decoder-btn">FIL: 250Hz</button>
            </div>
            <div class="cw-waterfall-container">
                <canvas id="cwWaterfallCanvas" class="cw-waterfall-canvas"></canvas>
                <div id="cwFilterBand" class="cw-filter-band"></div>
                <div class="cw-waterfall-hint">Click to set freq, scroll to adjust</div>
            </div>
            <div id="cwDecoderText" class="cw-decoder-text"></div>
        `;

        macroGrid2.parentNode.insertBefore(decoderSection, macroGrid2.nextSibling);

        waterfallCanvas = document.getElementById('cwWaterfallCanvas');
        if (waterfallCanvas) {
            waterfallCtx = waterfallCanvas.getContext('2d');
            setupCanvasSizing();
        }

        setupEventListeners();
        addDecoderStyles();
    }

    function setupCanvasSizing() {
        if (!waterfallCanvas) return;
        const container = waterfallCanvas.parentElement;
        if (!container) return;

        const rect = container.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;

        const width = Math.max(1, Math.floor((rect.width || 300) * dpr));
        const height = Math.max(1, Math.floor((rect.height || 128) * dpr));

        waterfallCanvas.width = width;
        waterfallCanvas.height = height;

        frequencyData = new Uint8Array(FFT_SIZE / 2);
    }

    function setupEventListeners() {
        document.getElementById('cwDecoderToggle')?.addEventListener('click', toggleDecoder);
        document.getElementById('cwDecoderGain')?.addEventListener('click', toggleGain);
        document.getElementById('cwDecoderFilter')?.addEventListener('click', toggleFilterWidth);

        if (waterfallCanvas) {
            waterfallCanvas.addEventListener('click', handleCanvasClick);
            waterfallCanvas.addEventListener('wheel', handleCanvasWheel, { passive: false });
        }

        const resizeObserver = new ResizeObserver(() => setupCanvasSizing());
        if (waterfallCanvas?.parentElement) {
            resizeObserver.observe(waterfallCanvas.parentElement);
        }
    }

    function watchCWBarVisibility() {
        const cwBar = document.getElementById('cwBar');
        if (!cwBar) return;

        const observer = new MutationObserver((mutations) => {
            mutations.forEach((mutation) => {
                if (mutation.type === 'attributes' && mutation.attributeName === 'class') {
                    const isVisible = !cwBar.classList.contains('hidden');
                    if (isVisible && waterfallCanvas) {
                        setupCanvasSizing();
                        if (decoderState.enabled && waterfallCtx) {
                            waterfallCtx.fillStyle = '#000';
                            waterfallCtx.fillRect(0, 0, waterfallCanvas.width, waterfallCanvas.height);
                        }
                    }
                }
            });
        });
        observer.observe(cwBar, { attributes: true });
    }

    function addDecoderStyles() {
        if (document.getElementById('cwDecoderStyles')) return;

        const style = document.createElement('style');
        style.id = 'cwDecoderStyles';
        style.textContent = `
            .cw-decoder-section { margin: 8px 0; border-top: 1px solid #0a0; padding-top: 8px; }
            .cw-decoder-controls { display: flex; gap: 8px; margin-bottom: 8px; justify-content: center; }
            .cw-decoder-btn { padding: 4px 12px; background: #001a00; border: 1px solid #0a0; color: #0a0; font-size: 11px; font-weight: bold; cursor: pointer; font-family: monospace; }
            .cw-decoder-btn:hover { background: #0a0; color: #000; }
            .cw-decoder-btn.active { background: #0a0; color: #000; }
            .cw-decoder-btn.loading { background: #1a1a00; border-color: #aa0; color: #aa0; }
            .cw-waterfall-container { position: relative; width: 100%; height: 128px; background: #000; border: 1px solid #0a0; border-radius: 4px; overflow: hidden; }
            .cw-waterfall-canvas { display: block; width: 100%; height: 100%; }
            .cw-filter-band { position: absolute; left: 0; right: 0; pointer-events: none; border-top: 2px solid #f00; border-bottom: 2px solid #f00; display: none; background: rgba(255, 0, 0, 0.1); }
            .cw-filter-band.active { display: block; }
            .cw-waterfall-hint { position: absolute; bottom: 4px; left: 50%; transform: translateX(-50%); font-size: 10px; color: #0a0; background: rgba(0, 0, 0, 0.8); padding: 2px 8px; border-radius: 2px; pointer-events: none; }
            .cw-decoder-text { background: #000; border: 1px solid #0a0; border-radius: 4px; padding: 8px; margin-top: 8px; min-height: 32px; font-family: 'Courier New', monospace; font-size: 18px; color: #0f0; white-space: pre-wrap; display: flex; flex-wrap: wrap; gap: 0; }
            .cw-decoder-char { display: inline-block; }
            .cw-decoder-char.abbrev { color: #ff0; font-weight: bold; }
        `;
        document.head.appendChild(style);
    }

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
        updateButtonStates();

        try {
            // Get wfview's audio context
            audioContext = window.audioCtx;
            if (!audioContext) {
                console.error('[CW Decoder] No audio context available');
                decoderState.loading = false;
                updateButtonStates();
                return;
            }

            // Create audio nodes
            analyserNode = audioContext.createAnalyser();
            analyserNode.fftSize = FFT_SIZE;
            analyserNode.smoothingTimeConstant = 0;
            analyserNode.minDecibels = -90;
            analyserNode.maxDecibels = -30;

            gainNode = audioContext.createGain();
            gainNode.gain.value = Math.pow(10, decoderState.gain / 20);

            // Connect to wfview's audio output
            // We need to insert our analyser between the final audio node and the destination
            connectToWfviewAudio();

            // Initialize worker for inference
            decoderWorker = new Worker('cw-decoder-worker.js');
            decoderWorker.onmessage = handleWorkerMessage;
            decoderWorker.postMessage({ type: 'loadModel' });

            // Wait for model to load
            await new Promise((resolve, reject) => {
                const timeout = setTimeout(() => reject(new Error('Model load timeout')), 30000);
                const checkLoaded = setInterval(() => {
                    if (decoderState.loaded) {
                        clearInterval(checkLoaded);
                        clearTimeout(timeout);
                        resolve();
                    }
                }, 100);
            });

            // Start waterfall
            setupCanvasSizing();
            waterfallCtx.fillStyle = '#000';
            waterfallCtx.fillRect(0, 0, waterfallCanvas.width, waterfallCanvas.height);
            startWaterfall();

            decoderState.enabled = true;
            decoderState.isDecoding = true;

        } catch (error) {
            console.error('[CW Decoder] Failed to start:', error);
            stopDecoder();
        } finally {
            decoderState.loading = false;
            updateButtonStates();
            updateFilterBand();
        }
    }

    function connectToWfviewAudio() {
        console.log('[CW Decoder] Connecting to audio pipeline...');
        console.log('[CW Decoder] audioWorkletNode:', !!window.audioWorkletNode);
        console.log('[CW Decoder] audioScriptNode:', !!window.audioScriptNode);
        console.log('[CW Decoder] audioGainNode:', !!window.audioGainNode);

        if (window.audioGainNode && window.audioWorkletNode) {
            // Disconnect the worklet from gain node
            window.audioWorkletNode.disconnect();

            // Connect worklet -> our gain -> our analyser -> wfview's gain -> destination
            window.audioWorkletNode.connect(gainNode);
            gainNode.connect(analyserNode);
            analyserNode.connect(window.audioGainNode);

            console.log('[CW Decoder] Connected to audio pipeline');
        } else if (window.audioGainNode && window.audioScriptNode) {
            // Fallback for ScriptProcessorNode
            window.audioScriptNode.disconnect();
            window.audioScriptNode.connect(gainNode);
            gainNode.connect(analyserNode);
            analyserNode.connect(window.audioGainNode);

            console.log('[CW Decoder] Connected to audio pipeline (ScriptProcessor)');
        } else {
            console.warn('[CW Decoder] Could not find audio nodes to connect to');
        }
    }

    function disconnectFromWfviewAudio() {
        // Restore original connection
        if (window.audioGainNode) {
            if (window.audioWorkletNode) {
                gainNode?.disconnect();
                analyserNode?.disconnect();
                window.audioWorkletNode.disconnect();
                window.audioWorkletNode.connect(window.audioGainNode);
            } else if (window.audioScriptNode) {
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

        disconnectFromWfviewAudio();

        if (decoderWorker) {
            decoderWorker.terminate();
            decoderWorker = null;
        }

        decoderState.enabled = false;
        decoderState.isDecoding = false;
        decoderState.loaded = false;
        decoderState.loading = false;

        clearDecoderText();
        updateButtonStates();
    }

    function handleWorkerMessage(event) {
        const msg = event.data;
        switch (msg.type) {
            case 'modelLoaded':
                decoderState.loaded = true;
                break;
            case 'inferenceResult':
                decoderState.currentSegments = msg.segments || [];
                updateDecoderText();
                break;
            case 'error':
                console.error('[CW Decoder Worker] Error:', msg.error);
                break;
        }
    }

    function toggleGain() {
        decoderState.gain = decoderState.gain === 0 ? 20 : 0;
        if (gainNode) {
            gainNode.gain.value = Math.pow(10, decoderState.gain / 20);
        }
        updateButtonStates();
    }

    function toggleFilterWidth() {
        decoderState.filterWidth = decoderState.filterWidth === 100 ? 250 : 100;
        updateButtonStates();
        updateFilterBand();
    }

    function updateButtonStates() {
        const toggleBtn = document.getElementById('cwDecoderToggle');
        const gainBtn = document.getElementById('cwDecoderGain');
        const filterBtn = document.getElementById('cwDecoderFilter');

        if (toggleBtn) {
            if (decoderState.loading) {
                toggleBtn.textContent = 'LOADING...';
                toggleBtn.className = 'cw-decoder-btn loading';
            } else if (decoderState.enabled) {
                toggleBtn.textContent = 'DECODE: ON';
                toggleBtn.className = 'cw-decoder-btn active';
            } else {
                toggleBtn.textContent = 'DECODE: OFF';
                toggleBtn.className = 'cw-decoder-btn';
            }
        }

        if (gainBtn) {
            gainBtn.textContent = 'GAIN: ' + decoderState.gain + 'dB';
            gainBtn.className = 'cw-decoder-btn' + (decoderState.gain === 20 ? ' active' : '');
        }

        if (filterBtn) {
            filterBtn.textContent = 'FIL: ' + decoderState.filterWidth + 'Hz';
        }
    }

    function startWaterfall() {
        console.log('[CW Decoder] startWaterfall called, canvas:', !!waterfallCanvas, 'analyser:', !!analyserNode);
        if (!waterfallCanvas || !analyserNode) {
            console.error('[CW Decoder] Missing canvas or analyser');
            return;
        }

        frequencyData = new Uint8Array(analyserNode.frequencyBinCount);
        renderState.lastTime = performance.now();
        renderState.pixelAccumulator = 0;

        renderWaterfall();
    }

    function renderWaterfall() {
        if (!decoderState.enabled || !waterfallCanvas || !analyserNode) {
            console.log('[CW Decoder] Render skipped:', { enabled: decoderState.enabled, hasCanvas: !!waterfallCanvas, hasAnalyser: !!analyserNode });
            return;
        }

        // Debug: log first few renders
        if (!window._cwDebugCount) window._cwDebugCount = 0;
        if (window._cwDebugCount < 5) {
            console.log('[CW Decoder] renderWaterfall running');
            window._cwDebugCount++;
        }

        const now = performance.now();
        const dt = (now - renderState.lastTime) / 1000;
        renderState.lastTime = now;

        const canvas = waterfallCanvas;
        const ctx = waterfallCtx;

        // Calculate scroll step
        const pxPerSec = canvas.width / BUFFER_DURATION_S;
        renderState.pixelAccumulator += dt * pxPerSec;

        let step = Math.floor(renderState.pixelAccumulator);
        if (step <= 0) {
            rafId = requestAnimationFrame(renderWaterfall);
            return;
        }
        renderState.pixelAccumulator -= step;
        if (step > canvas.width) step = canvas.width;

        // Get frequency data from analyser
        analyserNode.getByteFrequencyData(frequencyData);

        // Debug: check if we have any signal
        if (!window._cwDataDebug) window._cwDataDebug = 0;
        if (window._cwDataDebug < 10) {
            let maxVal = 0;
            for (let i = 0; i < frequencyData.length; i++) maxVal = Math.max(maxVal, frequencyData[i]);
            console.log('[CW Decoder] Max freq value:', maxVal);
            window._cwDataDebug++;
        }

        // Scroll existing image left
        ctx.drawImage(canvas, 0, 0, canvas.width, canvas.height, -step, 0, canvas.width, canvas.height);

        // Draw new column(s)
        const nyquist = audioContext.sampleRate / 2;
        const minBin = Math.floor((MIN_FREQ / nyquist) * frequencyData.length);
        const maxBin = Math.min(frequencyData.length, Math.floor((MAX_FREQ / nyquist) * frequencyData.length));
        const binCount = maxBin - minBin;

        for (let i = 0; i < step; i++) {
            const column = ctx.createImageData(1, canvas.height);
            const buf = column.data;

            for (let y = 0; y < canvas.height; y++) {
                // Map canvas Y (inverted) to frequency bin
                const freqRatio = (canvas.height - 1 - y) / Math.max(1, canvas.height - 1);
                const binIdx = minBin + Math.floor(freqRatio * binCount);
                const clampedIdx = Math.min(Math.max(binIdx, 0), frequencyData.length - 1);
                const value = frequencyData[clampedIdx] || 0;
                const [r, g, b] = colorLUT[value];
                const p = y * 4;
                buf[p] = r;
                buf[p + 1] = g;
                buf[p + 2] = b;
                buf[p + 3] = 255;
            }

            ctx.putImageData(column, canvas.width - step + i, 0);
        }

        // Run inference periodically
        if (now - lastInferenceTime > INFERENCE_INTERVAL_MS) {
            runInference();
            lastInferenceTime = now;
        }

        rafId = requestAnimationFrame(renderWaterfall);
    }

    function runInference() {
        if (!decoderState.enabled || !decoderWorker || !decoderState.loaded || !analyserNode) return;

        // Get time domain data for inference
        const timeData = new Float32Array(analyserNode.fftSize);
        analyserNode.getFloatTimeDomainData(timeData);

        // TODO: Downsample to 3200 Hz and accumulate 12 seconds of audio
        // For now, skip inference until we implement proper resampling

        // decoderWorker.postMessage({
        //     type: 'runInference',
        //     audioBuffer: resampledBuffer,
        //     filterFreq: decoderState.filterFreq,
        //     filterWidth: decoderState.filterWidth
        // });
    }

    function updateFilterBand() {
        const band = document.getElementById('cwFilterBand');
        if (!band) return;

        if (decoderState.filterFreq === null) {
            band.classList.remove('active');
            return;
        }

        const range = MAX_FREQ - MIN_FREQ;
        const half = decoderState.filterWidth / 2;
        const lower = Math.max(MIN_FREQ, decoderState.filterFreq - half);
        const upper = Math.min(MAX_FREQ, decoderState.filterFreq + half);

        const topPercent = ((MAX_FREQ - upper) / range) * 100;
        const heightPercent = ((upper - lower) / range) * 100;

        band.style.top = topPercent + '%';
        band.style.height = heightPercent + '%';
        band.classList.add('active');
    }

    function handleCanvasClick(event) {
        const rect = waterfallCanvas.getBoundingClientRect();
        const y = event.clientY - rect.top;

        if (decoderState.filterFreq !== null) {
            decoderState.filterFreq = null;
        } else {
            const canvasHeight = rect.height;
            const invY = canvasHeight - y;
            const freqRange = MAX_FREQ - MIN_FREQ;
            const rawFreq = MIN_FREQ + (invY / canvasHeight) * freqRange;
            const halfWidth = decoderState.filterWidth / 2;
            let freq = rawFreq;
            if (freq - halfWidth < 400) freq = 400 + halfWidth;
            if (freq + halfWidth > 1200) freq = 1200 - halfWidth;
            decoderState.filterFreq = Math.round(freq);
        }

        updateFilterBand();
    }

    function handleCanvasWheel(event) {
        event.preventDefault();
        if (decoderState.filterFreq === null) return;

        const step = 20;
        let newFreq = decoderState.filterFreq + (event.deltaY < 0 ? step : -step);

        const halfWidth = decoderState.filterWidth / 2;
        if (newFreq - halfWidth < 400) newFreq = 400 + halfWidth;
        if (newFreq + halfWidth > 1200) newFreq = 1200 - halfWidth;

        decoderState.filterFreq = Math.round(newFreq);
        updateFilterBand();
    }

    function updateDecoderText() {
        const display = document.getElementById('cwDecoderText');
        if (!display) return;

        display.innerHTML = '';
        decoderState.currentSegments.forEach(segment => {
            Array.from(segment.text).forEach(char => {
                const span = document.createElement('span');
                span.className = 'cw-decoder-char' + (segment.isAbbreviation ? ' abbrev' : '');
                span.textContent = char;
                display.appendChild(span);
            });
        });
    }

    function clearDecoderText() {
        const display = document.getElementById('cwDecoderText');
        if (display) display.innerHTML = '';
    }

    // Expose API
    window.CWDecoder = {
        init: initDecoder,
        get state() { return decoderState; }
    };

    // Auto-init
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initDecoder);
    } else {
        initDecoder();
    }
})();
