// Simulates this Windows box's audio inputs inside the wfweb SPA so the mic
// device logic can be exercised where real capture is blocked.  The browser
// "default" resolves to the rig's own USB codec, exactly as Brave did here.
(function () {
  var RIG = 'Microphone (USB Audio CODEC ) (08bb:2901)';
  var REALTEK = 'Microphone Array (Realtek(R) Audio)';
  var devices = [
    { kind: 'audioinput', deviceId: 'default', groupId: 'g1', label: 'Default - ' + RIG },
    { kind: 'audioinput', deviceId: 'communications', groupId: 'g1', label: 'Communications - ' + RIG },
    { kind: 'audioinput', deviceId: 'id-realtek', groupId: 'g2', label: REALTEK },
    { kind: 'audioinput', deviceId: 'id-rig', groupId: 'g1', label: RIG },
    { kind: 'audiooutput', deviceId: 'default', groupId: 'g3', label: 'Default - Speakers (Realtek(R) Audio)' }
  ];
  window.__micCalls = [];
  var ctx = null;
  function fakeStream(label) {
    if (!ctx) ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 48000 });
    var osc = ctx.createOscillator(); osc.frequency.value = 1000;
    var gain = ctx.createGain(); gain.gain.value = 0.25;
    var dest = ctx.createMediaStreamDestination();
    osc.connect(gain); gain.connect(dest); osc.start();
    var s = dest.stream;
    var real = s.getAudioTracks()[0];
    var track = { kind: 'audio', label: label, enabled: true, readyState: 'live',
                  stop: function () { real.stop(); try { osc.stop(); } catch (e) {} },
                  getSettings: function () { return { sampleRate: 48000, channelCount: 1 }; } };
    Object.defineProperty(s, 'getAudioTracks', { value: function () { return [track]; } });
    Object.defineProperty(s, 'getTracks', { value: function () { return [track]; } });
    return s;
  }
  navigator.mediaDevices.enumerateDevices = function () {
    window.__micCalls.push({ enumerate: true });
    return Promise.resolve(devices.map(function (d) { return Object.assign({}, d); }));
  };
  navigator.mediaDevices.getUserMedia = function (c) {
    var want = c && c.audio && c.audio.deviceId && c.audio.deviceId.exact;
    window.__micCalls.push({ getUserMedia: want || '(default)' });
    if (!want) return Promise.resolve(fakeStream(RIG));
    var d = devices.filter(function (x) { return x.deviceId === want; })[0];
    if (!d) { var e = new Error('OverconstrainedError: ' + want); e.name = 'OverconstrainedError'; return Promise.reject(e); }
    return Promise.resolve(fakeStream(d.label.replace(/^(Default|Communications) - /, '')));
  };
  return 'stubbed: ' + devices.length + ' devices';
})();
