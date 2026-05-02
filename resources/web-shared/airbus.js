// Shared in-browser audio bus for virtual rigs.
//
// Each tab that participates joins a same-origin BroadcastChannel and tags
// its TX with a small integer rigId so it can drop its own echo. Phase 1
// is a dumb broadcast — every other tab hears every TX, regardless of
// frequency or mode. Channel/mode gating lands in a later phase.
//
// Wire format (postMessage payload):
//   { rigId: int, samples: Int16Array }
// The Int16Array's underlying buffer is transferred for zero-copy.

(function (global) {
    'use strict';

    var CHANNEL = 'wfweb-airbus';

    class AirBus extends EventTarget {
        constructor(rigId) {
            super();
            this.rigId = rigId | 0;
            this._chan = new BroadcastChannel(CHANNEL);
            var self = this;
            this._chan.onmessage = function (ev) {
                var msg = ev.data;
                if (!msg || (msg.rigId | 0) === self.rigId) return;
                if (!(msg.samples instanceof Int16Array)) return;
                self.dispatchEvent(new CustomEvent('rx', { detail: msg }));
            };
        }

        // samples: Int16Array whose buffer will be transferred away.
        tx(samples) {
            if (!(samples instanceof Int16Array) || samples.length === 0) return;
            try {
                this._chan.postMessage({ rigId: this.rigId, samples: samples }, [samples.buffer]);
            } catch (e) {
                console.warn('airbus tx:', e);
            }
        }

        close() {
            try { this._chan.close(); } catch (e) { /* ignore */ }
        }
    }

    global.AirBus = AirBus;
})(window);
