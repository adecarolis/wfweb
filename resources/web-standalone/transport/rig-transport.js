// Abstract transport for rig control. Two concrete implementations:
//   WebSocketRigTransport — talks to the C++ wfweb server over ws://
//   SerialRigTransport    — talks to the rig directly via Web Serial CI-V (Phase 1)
//
// Outbound surface:
//   sendCommand(obj)        — control plane (JSON in WS; CI-V bytes in Serial)
//   sendAudioFrame(buffer)  — type 0x03 audio frame (Phase 2 makes this a no-op
//                             in Direct mode once rig audio is routed via setSinkId)
//
// Inbound surface (EventTarget):
//   'open'    — link is up
//   'close'   — link is down (detail: original event)
//   'error'   — transport error (detail: original event)
//   'message' — JSON message from server (detail: parsed object)
//   'binary'  — binary frame (detail: ArrayBuffer)
//
// Reconnection is the caller's responsibility (index.html owns scheduleReconnect()).

(function (global) {
    'use strict';

    class RigTransport extends EventTarget {
        connect() { throw new Error('RigTransport.connect not implemented'); }
        close()   { throw new Error('RigTransport.close not implemented'); }
        isOpen()  { return false; }
        sendCommand(/* obj */)     { throw new Error('RigTransport.sendCommand not implemented'); }
        sendAudioFrame(/* buf */)  { throw new Error('RigTransport.sendAudioFrame not implemented'); }
    }

    global.RigTransport = RigTransport;
})(window);
