#!/usr/bin/env bash
# Orchestrate the virtual-rig test bench: one virtualrig + N wfweb instances,
# each wfweb connecting over LAN UDP to its own virtual IC-7610.
#
# Usage:
#   ./scripts/testrig.sh up [N] [virtualrig flags...]   (default N=2, max 16)
#   ./scripts/testrig.sh down
#   ./scripts/testrig.sh status
#   ./scripts/testrig.sh logs [i|virtualrig]
#
# Any arguments after N are forwarded to the virtualrig binary, e.g.:
#   ./scripts/testrig.sh up 3 --noise 500 --atten 0.3 --broadcast
#
# Scratch dir .testrig/ holds PIDs, logs, and per-instance settings files.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$SCRIPT_DIR/.." && pwd)
SCRATCH="$REPO/.testrig"
VIRTUALRIG="$REPO/tools/virtualrig/virtualrig"
WFWEB="$REPO/wfweb"
BASE_PORT=50001      # virtualrig rig 0 control port; civ=+1, audio=+2, next rig=+10
WEB_BASE=9080        # wfweb #i serves HTTPS on WEB_BASE+i*10, WebSocket on WEB_BASE+i*10+1
WEB_STEP=10          # wfweb also binds webPort+1 for WebSocket, so stride must be >= 2
CTRL_PORT=5900       # virtualrig control panel (HTTP)

die() { echo "testrig: $*" >&2; exit 1; }

require_binaries() {
    [[ -x "$VIRTUALRIG" ]] || die "virtualrig not built. Run: cd tools/virtualrig && qmake && make -j\$(nproc)"
    [[ -x "$WFWEB" ]]      || die "wfweb not built. Run: qmake wfweb.pro && make -j\$(nproc)"
}

pid_alive() {
    local pidfile=$1
    [[ -f "$pidfile" ]] || return 1
    local pid
    pid=$(cat "$pidfile" 2>/dev/null) || return 1
    [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null
}

stop_pidfile() {
    local pidfile=$1
    [[ -f "$pidfile" ]] || return 0
    local pid
    pid=$(cat "$pidfile" 2>/dev/null) || { rm -f "$pidfile"; return 0; }
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true
        # Give it ~3s to exit gracefully.
        for _ in 1 2 3 4 5 6; do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.5
        done
        kill -0 "$pid" 2>/dev/null && kill -KILL "$pid" 2>/dev/null || true
    fi
    rm -f "$pidfile"
}

count_existing_wfwebs() {
    # Echo the highest-numbered wfweb_<i>.pid that exists, plus 1 (the count).
    local i=0
    while [[ -f "$SCRATCH/wfweb_${i}.pid" ]]; do
        i=$((i + 1))
    done
    echo "$i"
}

wait_for_web() {
    local port=$1
    local deadline=$(( $(date +%s) + 15 ))
    while (( $(date +%s) < deadline )); do
        # -k: skip TLS verify (self-signed). -o /dev/null -w %{http_code}: just care if it answers.
        local code
        code=$(curl -sk -o /dev/null -w '%{http_code}' --connect-timeout 1 --max-time 2 \
            "https://127.0.0.1:${port}/" 2>/dev/null || echo "000")
        # Any non-000 code means the HTTP server is listening and replying.
        if [[ "$code" != "000" ]]; then
            return 0
        fi
        sleep 0.5
    done
    return 1
}

cmd_up() {
    require_binaries
    local n=${1:-2}
    shift || true
    # Anything left is a passthrough to virtualrig (--noise, --atten, etc.).
    local extra_args=("$@")
    [[ "$n" =~ ^[0-9]+$ ]] || die "N must be an integer"
    (( n >= 1 && n <= 16 )) || die "N must be 1..16"

    if pid_alive "$SCRATCH/virtualrig.pid"; then
        die "test rig already running (.testrig/virtualrig.pid). Run: $0 down"
    fi

    mkdir -p "$SCRATCH"

    echo "testrig: starting virtualrig (N=$n, base port $BASE_PORT)"
    # Note: the subshell groups the cd with the launch but the `&` inside it
    # backgrounds the nohup itself (not the enclosing subshell), so `$!` is the
    # real virtualrig PID. Writing it inside the subshell keeps the whole thing
    # local — we don't touch the parent shell's cwd.
    (
        cd "$REPO"
        nohup "$VIRTUALRIG" --rigs "$n" --base-port "$BASE_PORT" "${extra_args[@]}" \
            > "$SCRATCH/virtualrig.log" 2>&1 &
        echo $! > "$SCRATCH/virtualrig.pid"
    )

    # Give virtualrig a moment to bind sockets.
    sleep 1
    if ! pid_alive "$SCRATCH/virtualrig.pid"; then
        echo "testrig: virtualrig exited immediately. Last log lines:" >&2
        tail -n 20 "$SCRATCH/virtualrig.log" >&2 || true
        exit 1
    fi

    for (( i=0; i<n; i++ )); do
        local ctrl=$(( BASE_PORT + i*10 ))
        local civ=$((  BASE_PORT + i*10 + 1 ))
        local aud=$((  BASE_PORT + i*10 + 2 ))
        local web=$(( WEB_BASE + i*WEB_STEP ))
        local settings="$SCRATCH/wfweb_${i}.ini"
        local log="$SCRATCH/wfweb_${i}.log"
        local pidfile="$SCRATCH/wfweb_${i}.pid"

        echo "testrig: starting wfweb #$i (web :$web -> rig LAN :$ctrl/$civ/$aud)"
        (
            cd "$REPO"
            nohup "$WFWEB" \
                -s "$settings" \
                -l "$log" \
                -p "$web" \
                --lan 127.0.0.1 \
                --lan-control "$ctrl" \
                --lan-serial  "$civ" \
                --lan-audio   "$aud" \
                --lan-user    wfweb \
                --lan-pass    wfweb \
                </dev/null >/dev/null 2>&1 &
            echo $! > "$pidfile"
        )
    done

    # Poll each web server until it responds, so we don't print URLs that 404.
    echo "testrig: waiting for web UIs to come up..."
    for (( i=0; i<n; i++ )); do
        local web=$(( WEB_BASE + i*WEB_STEP ))
        if ! wait_for_web "$web"; then
            echo "testrig: wfweb #$i (:$web) did not become ready within 15s." >&2
            echo "        See: $SCRATCH/wfweb_${i}.log" >&2
        fi
    done

    # Labels match virtualrig's "virtual-IC7300-A..P" naming.
    local labels="ABCDEFGHIJKLMNOP"
    echo
    echo "Test rig is up (N=$n)."
    echo
    for (( i=0; i<n; i++ )); do
        local web=$(( WEB_BASE + i*WEB_STEP ))
        local label=${labels:i:1}
        printf '  Rig #%d  "virtual-IC7300-%s"   https://127.0.0.1:%d\n' "$i" "$label" "$web"
    done
    echo
    printf '  Bench control panel:               http://127.0.0.1:%d\n' "$CTRL_PORT"
    echo
    echo "virtualrig log:  $SCRATCH/virtualrig.log"
    echo "wfweb logs:      $SCRATCH/wfweb_{0..$((n-1))}.log"
    echo
    echo "Stop with:       $0 down"
}

cmd_down() {
    [[ -d "$SCRATCH" ]] || { echo "testrig: nothing to stop."; return 0; }
    local stopped=0
    for pidfile in "$SCRATCH"/wfweb_*.pid; do
        [[ -e "$pidfile" ]] || continue
        stop_pidfile "$pidfile"
        stopped=$((stopped + 1))
    done
    if [[ -f "$SCRATCH/virtualrig.pid" ]]; then
        stop_pidfile "$SCRATCH/virtualrig.pid"
        stopped=$((stopped + 1))
    fi
    if (( stopped == 0 )); then
        echo "testrig: nothing was running."
    else
        echo "testrig: stopped $stopped process(es). Logs kept in $SCRATCH/."
    fi
}

cmd_status() {
    [[ -d "$SCRATCH" ]] || { echo "testrig: not running (no $SCRATCH)."; return 0; }
    if pid_alive "$SCRATCH/virtualrig.pid"; then
        echo "virtualrig: UP (pid $(cat "$SCRATCH/virtualrig.pid"))"
    else
        echo "virtualrig: down"
    fi
    local n
    n=$(count_existing_wfwebs)
    for (( i=0; i<n; i++ )); do
        local pidfile="$SCRATCH/wfweb_${i}.pid"
        local web=$(( WEB_BASE + i*WEB_STEP ))
        if pid_alive "$pidfile"; then
            printf 'wfweb #%d:   UP (pid %s)   https://127.0.0.1:%d\n' \
                "$i" "$(cat "$pidfile")" "$web"
        else
            printf 'wfweb #%d:   down\n' "$i"
        fi
    done
}

cmd_logs() {
    local which=${1:-virtualrig}
    local path
    case "$which" in
        virtualrig|vr|rig) path="$SCRATCH/virtualrig.log" ;;
        [0-9]|[0-9][0-9])  path="$SCRATCH/wfweb_${which}.log" ;;
        *) die "logs target must be 'virtualrig' or a wfweb index (0..N-1)" ;;
    esac
    [[ -f "$path" ]] || die "log not found: $path"
    tail -F "$path"
}

main() {
    local cmd=${1:-}
    shift || true
    case "$cmd" in
        up)     cmd_up "$@" ;;
        down)   cmd_down "$@" ;;
        status) cmd_status "$@" ;;
        logs)   cmd_logs "$@" ;;
        ''|help|-h|--help)
            sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'
            ;;
        *) die "unknown command: $cmd (try: up | down | status | logs)" ;;
    esac
}

main "$@"
