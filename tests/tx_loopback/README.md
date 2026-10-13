# Windows TX-audio loopback check (no RF)

Verifies the browser -> wfweb -> USB codec playback path on Windows without
keying the radio.  `txfeed.py` replays exactly what the SPA's mic worklet
sends (enableMic, then 256-byte PCM frames every 2.67 ms) with a steady tone;
`loopback.py` records what Windows actually renders to the rig's playback
endpoint (WASAPI loopback) and reports every gap.

    pip install --user pyaudiowpatch numpy websockets
    python loopback.py rec --device "USB Audio CODEC" --seconds 14 --out cap.wav --tone 1000 &
    sleep 2; python txfeed.py --seconds 10 --tone 1000 [--burst 8] [--stall-at 4 --stall-ms 300]

Reference results, IC-7300 on COM4, 2026-10-13:
  0.9.2 (3a5ae69): 457 gaps in a 10 s tone   (issue #136, chopped audio)
  dev   (7b3b6a6):   0 gaps, also with 8-frame bursts; a 300 ms source
                     stall inserts silence and resumes without loss

## Browser mic selection (mic_stub.js)

Paste `mic_stub.js` into the SPA's devtools console (or run it with a browser
automation tool) to replace getUserMedia/enumerateDevices with a simulated
Windows device list whose browser default is the rig's own "USB Audio CODEC"
capture.  Then turn TX AUDIO on: AUTO must switch to the Realtek input and the
server log must say "switched automatically"; the MIC tile cycles AUTO ->
Realtek -> rig codec (server warns, indicator reads "MIC = RADIO RX") -> AUTO.
To load the page in a browser that rejects the self-signed certificate, run a
copy of `wfweb-release` without the `libssl-*`/`libcrypto-*` DLLs: the server
then serves plain HTTP on 8080 (localhost is still a secure context).
