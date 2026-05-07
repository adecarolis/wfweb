# Plan: rigctld support (Hamlib TCP protocol)

Re-introduce upstream wfview's rigctld emulation so external clients
(POTACAT, fldigi, WSJT-X, gpredict, ...) can drive a wfweb server build
over the Hamlib TCP protocol on port 4532.

Tracks GitHub issue #64.

## Approach: option C — port + TX gateway

Port upstream `rigctld.cpp` largely verbatim (1626 LOC, all referenced
types/funcs/queue methods are still present in wfweb). Replace only the
two PTT call sites with a signal that routes through `webServer`, so
RADE EOO synthesis, packet `packetTxBusy` gating, and ALC meter polling
stay coherent.

Server build only. Standalone is browser-only and can't open TCP.

## Branch

`feature/rigctld-support`

## File-by-file change set

### New file: `src/rigctld.cpp`

Port from `gitlab.com/eliggett/wfview:src/rigctld.cpp@master` (1626 LOC).
Edits against the upstream copy:

1. PTT (search for `funcTransceiverStatus` in `getCommand()` `typeBinary`
   case). Two call sites — the set path inside the `commands_list` `'T'`
   handler. Replace
   ```cpp
   queue->add(priorityImmediate, queueItem(func, val, false, state.receiver));
   ```
   with
   ```cpp
   if (func == funcTransceiverStatus)
       emit parent->pttRequested(val.toBool());
   else
       queue->add(priorityImmediate, queueItem(func, val, false, state.receiver));
   ```
2. Same treatment for `funcSendCW` (CW pacing should go through webserver).
3. Replace `WFVIEW(%0)` model string in `dump_conf` (line 514) with
   `WFWEB(%0)`.

No other edits expected.

### `include/rigctld.h` (existing, orphaned)

Add one signal:
```cpp
signals:
    void pttRequested(bool on);
    void cwRequested(QString text);  // optional, see step 2 above
```

### `wfweb.pro`

Add to `SOURCES`:
```
    src/rigctld.cpp \
```
(`include/rigctld.h` is already in `HEADERS`.)

### `include/servermain.h`

In `struct preferences` (line 265):
```cpp
    quint16 rigCtlPort = 4532;
    bool rigCtlEnabled = false;
    bool rigCtlBindAll = false;   // default: localhost only
```

As class members alongside `web`/`webThread`:
```cpp
    rigCtlD* rigctl = Q_NULLPTR;
```

(Lives in `webThread`, not its own thread — see "Threading" below.)

Forward-declare `class rigCtlD;` near the top of the header.

### `src/servermain.cpp`

1. `setDefPrefs()` (around line 580):
   ```cpp
   defPrefs.rigCtlPort = 4532;
   defPrefs.rigCtlEnabled = false;
   defPrefs.rigCtlBindAll = false;
   ```
2. `loadSettings()` (around line 610):
   ```cpp
   prefs.rigCtlPort = settings->value("RigCtldPort", defPrefs.rigCtlPort).toInt();
   prefs.rigCtlEnabled = settings->value("RigCtldEnabled", defPrefs.rigCtlEnabled).toBool();
   prefs.rigCtlBindAll = settings->value("RigCtldBindAll", defPrefs.rigCtlBindAll).toBool();
   ```
3. `applyCLIOverrides()`:
   - `--rigctld-port N` → `prefs.rigCtlPort = N; prefs.rigCtlEnabled = true;`
   - `--no-rigctld` → `prefs.rigCtlEnabled = false;`
   - `--rigctld-bind-all` → `prefs.rigCtlBindAll = true;`
4. After `web` is constructed and moved to `webThread`, before
   `webThread->start()`:
   ```cpp
   if (prefs.rigCtlEnabled) {
       rigctl = new rigCtlD();
       rigctl->moveToThread(webThread);
       rigctl->setBindAddress(prefs.rigCtlBindAll
                              ? QHostAddress::Any
                              : QHostAddress::LocalHost);
       connect(webThread, &QThread::finished, rigctl, &QObject::deleteLater);
       connect(rigctl, &rigCtlD::pttRequested,
               web, &webServer::onRigCtlPtt,
               Qt::QueuedConnection);
       QMetaObject::invokeMethod(rigctl, "startServer",
                                 Qt::QueuedConnection,
                                 Q_ARG(qint16, prefs.rigCtlPort));
   }
   ```
5. `startServer()` in upstream `rigctld.cpp` hardcodes
   `QHostAddress::Any` — add `setBindAddress(QHostAddress)` to
   `rigCtlD` and use it in `startServer()`. Default localhost.

### `src/main.cpp`

Add to CLI parsing (`overrides` already has the pattern):
```cpp
    QCommandLineOption rigCtldPortOpt("rigctld-port",
        "Enable rigctld on the given TCP port (default 4532).", "port");
    QCommandLineOption noRigCtldOpt("no-rigctld",
        "Disable rigctld even if enabled in settings.");
    QCommandLineOption rigCtldBindAllOpt("rigctld-bind-all",
        "Bind rigctld to 0.0.0.0 (default: localhost only).");
```
Wire into `cmdLineOverrides` mirror fields and into help text.

### `src/webserver.cpp`

Add slot `onRigCtlPtt(bool on)` that runs the same path as the
WebSocket `setPTT` handler at line 1497:

```cpp
void webServer::onRigCtlPtt(bool on)
{
    QJsonObject cmd;
    cmd["cmd"] = "setPTT";
    cmd["value"] = on;
    handleCommand(nullptr, cmd);
}
```

`handleCommand` already accepts `QWebSocket *client` as nullable for
broadcast paths — confirm by audit; if not, refactor the setPTT branch
out into a helper both call.

If we add `cwRequested`, do the same for `setSendCW`.

### `include/webserver.h`

Declare `void onRigCtlPtt(bool on);` as a public slot.

### `REST_API.md`

Add a short section near the top documenting the rigctld TCP port:

> **Hamlib rigctld** (option, server build only): TCP, default port
> 4532, bound to `127.0.0.1`. Enable with `--rigctld-port 4532` or
> `RigCtldEnabled=true` in settings. See `RIGCTLD.md` for protocol
> details.

### `WFSERVER.md`

Add a `--rigctld-port` row to the CLI flags table.

### `CHANGELOG`

```
[unreleased]
- Add rigctld (Hamlib TCP) emulation on port 4532 for compatibility
  with fldigi, WSJT-X, POTACAT, gpredict and other Hamlib clients.
  Disabled by default; enable with `--rigctld-port 4532` or
  `RigCtldEnabled=true` in settings. Binds to localhost unless
  `--rigctld-bind-all` is passed. (#64)
```

### `CLAUDE.md`

Two stale-doc fixes while we're touching things:
- Replace `wfmain` references with `servermain` in the "Key Files" and
  "Web Server" sections.
- Remove `include/prefs.h` row from "Key Files" (does not exist).
- Add `src/rigctld.cpp` and `include/rigctld.h` to "Key Files".

## Threading

`rigCtlD` lives in `webThread` (the existing webserver QThread).
Rationale:
- Shares `cachingQueue` (mutex-protected) with `webServer`.
- `pttRequested → onRigCtlPtt` becomes an in-thread queued connection,
  no cross-thread overhead beyond the event-loop hop.
- `incomingConnection()` runs in `webThread`; per-client `rigCtlClient`
  socket reads use `Qt::DirectConnection` per upstream — same thread.

`cachingQueue::getState()` already locks `QMutexLocker` so cross-thread
access from external callers stays safe regardless.

## Security default

Default bind: `QHostAddress::LocalHost`. Hamlib protocol is
unauthenticated; binding to a network interface hands PTT to anyone on
the LAN. `--rigctld-bind-all` is opt-in.

## Test plan

### Build
1. `qmake wfweb.pro && make -j$(nproc)` — must compile clean.
2. `./wfweb --packet-self-test` — ensure no regression in modem path.

### Smoke (no rig)
3. `./wfweb --rigctld-port 4532` (no rig connected): start, verify
   port listening, `nc 127.0.0.1 4532` then send `q` — clean disconnect.

### With IC-7300 over USB
4. `rigctl -m 2 -r 127.0.0.1:4532` — interactive Hamlib client.
   Exercise:
   - `f` (get_freq), `F 14074000` (set_freq)
   - `m` (get_mode), `M USB 2400` (set_mode + passband)
   - `v` (get_vfo), `V VFOB` (set_vfo)
   - `t` (get_ptt), `T 1` / `T 0` (set_ptt) — verify rig keys/unkeys
   - `l STRENGTH`, `l SWR`, `l RFPOWER_METER`
   - `L AF 0.5`, `L RFPOWER 0.3`
   - `\dump_state`, `\dump_caps`, `\chk_vfo`
5. `fldigi` configured for Hamlib → `IC-7300` (Hamlib model 3073 or
   whatever `rigCaps->rigctlModel` returns) on `127.0.0.1:4532`. Verify
   freq/mode set works and updates web UI.
6. `wsjtx` same as fldigi, verify TX cycle.
7. POTACAT (linked from #64) — full session per author's blog post.

### TX coordination
8. With FreeDV/RADE active in web UI, issue `T 1` then `T 0` from
   `rigctl`. Verify EOO frame is synthesized (look for "RADE delayed
   unkey" in log) — this validates the gateway path.
9. With packet enabled and a TX in flight (`packetTxBusy=true`), issue
   `T 1` from rigctl. Should be deferred or rejected per existing
   webserver coordination, NOT race-key the rig.

### Multi-client
10. Two `rigctl` sessions in parallel. Verify both can read state
    concurrently; last-writer-wins on set is acceptable.

### Default-deny
11. `./wfweb --rigctld-port 4532` (no `--rigctld-bind-all`). From
    another host on the LAN, attempt `nc <host> 4532` — must refuse.
    From the same host, must connect.

## Effort estimate

| Phase | Effort |
|-------|--------|
| Port + edits to `rigctld.cpp` | 3–4h |
| Wiring (`servermain`/`main`/`webserver`/`prefs`) | 2–3h |
| Build, fix any compile drift | 1–2h |
| Smoke + `rigctl` exhaustive test against IC-7300 | 2–3h |
| fldigi + WSJT-X + POTACAT integration testing | 3–4h |
| TX coordination + bind-default validation | 1–2h |
| Docs (`RIGCTLD.md`, `CHANGELOG`, `CLAUDE.md` fixes, `REST_API.md`) | 1–2h |
| **Total** | **~1.5–2 days** |

## Known risks

1. **`dump_state`/`dump_caps` quirks per client.** Hamlib clients are
   famously picky about the exact byte layout of `dump_state`. Bugs
   here surface as "rig connected but won't tune". Test with each
   target client; mostly inherited behavior from upstream wfview.
2. **No auth.** Mitigated by localhost default.
3. **Mode mapping for D-STAR/DV/DD/C4FM** in `mode_str[]` is
   approximate. Spot-check per supported rig if anyone uses these
   modes via rigctld.
4. **`getMode` lookup uses `rigCaps->modes` plus `mode_str` table.**
   For rigs with `inputs.size() > 0` (cmd29 rigs with separate data
   inputs), the matcher uses `mstr.contains(mode.name)` — verify that
   IC-7610/9700/905 mode names line up with the Hamlib labels.
5. **CLAUDE.md drift.** The doc still says `wfmain`/`include/prefs.h`.
   Plan includes the fix.

## Out of scope

- Standalone build (browsers can't open TCP listeners).
- Authentication (Hamlib has none; out of spec).
- IPv6 binding (upstream uses `QHostAddress::Any` v4 default; can add
  later if asked).
- `wfserver.pro` build target (wfweb does not have one — only
  `wfweb.pro`).
