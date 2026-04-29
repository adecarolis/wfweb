// WebSocket transport: thin wrapper over the existing ws:// connection to the
// C++ wfweb server. Behaviour matches the pre-refactor inline ws code in
// index.html exactly — same URL derivation, same JSON envelope, same binary
// frame format.

(function (global) {
    'use strict';

    function defaultUrlBuilder() {
        if (location.protocol === 'https:') {
            return 'wss://' + location.hostname + ':' + (location.port || '443');
        }
        var port = parseInt(location.port || '80') + 1;
        return 'ws://' + location.hostname + ':' + port;
    }

    class WebSocketRigTransport extends global.RigTransport {
        constructor(opts) {
            super();
            opts = opts || {};
            this.urlBuilder = opts.urlBuilder || defaultUrlBuilder;
            this.ws = null;
        }

        connect() {
            if (this.ws) {
                try { this.ws.close(); } catch (e) { /* ignore */ }
                this.ws = null;
            }
            var url = this.urlBuilder();
            var ws = new WebSocket(url);
            ws.binaryType = 'arraybuffer';
            var self = this;
            ws.onopen    = function ()  { self.dispatchEvent(new Event('open')); };
            ws.onclose   = function (e) { self.dispatchEvent(new CustomEvent('close', { detail: e })); };
            ws.onerror   = function (e) { self.dispatchEvent(new CustomEvent('error', { detail: e })); };
            ws.onmessage = function (event) {
                if (event.data instanceof ArrayBuffer) {
                    self.dispatchEvent(new CustomEvent('binary', { detail: event.data }));
                    return;
                }
                try {
                    var msg = JSON.parse(event.data);
                    self.dispatchEvent(new CustomEvent('message', { detail: msg }));
                } catch (err) {
                    console.error('WebSocketRigTransport parse error:', err);
                }
            };
            this.ws = ws;
        }

        close() {
            if (this.ws) {
                try { this.ws.close(); } catch (e) { /* ignore */ }
                this.ws = null;
            }
        }

        isOpen() {
            return !!(this.ws && this.ws.readyState === WebSocket.OPEN);
        }

        sendCommand(obj) {
            if (this.isOpen()) this.ws.send(JSON.stringify(obj));
        }

        sendAudioFrame(buffer) {
            if (this.isOpen()) this.ws.send(buffer);
        }
    }

    global.WebSocketRigTransport = WebSocketRigTransport;
})(window);
