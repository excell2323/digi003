#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BRIDGE="$PROJECT_DIR/Tools/bin/digi003-midi-bridge"
LOG_DIR="${LOG_DIR:-$HOME/Library/Logs/FireWireOHCIProbe}"
POLL_MS="${POLL_MS:-5}"
FEEDBACK_TO_DRIVER="${FEEDBACK_TO_DRIVER:-1}"
WAIT_FOR_VCONTROL="${WAIT_FOR_VCONTROL:-0}"
FEEDBACK_LOG="$LOG_DIR/digi003-midi-feedback.log"

log() {
    printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

driver_ready() {
    ioreg -l -r -c FireWireOHCIProbe 2>/dev/null |
        /usr/bin/grep -q 'IOUserServerCDHash'
}

vcontrol_ready() {
    pgrep -x "V-Control Pro" >/dev/null 2>&1 ||
        pgrep -x "VCPMidiServer" >/dev/null 2>&1
}

wait_for_driver() {
    until driver_ready; do
        log "waiting for FireWireOHCIProbe driver"
        sleep 2
    done
}

wait_for_vcontrol() {
    if [ "$WAIT_FOR_VCONTROL" != "1" ]; then
        return
    fi
    until vcontrol_ready; do
        log "waiting for V-Control"
        sleep 2
    done
}

if [ ! -x "$BRIDGE" ]; then
    "$PROJECT_DIR/scripts/build-tools.sh"
fi
mkdir -p "$LOG_DIR"

log "agent started"
while true; do
    wait_for_driver
    wait_for_vcontrol

    log "starting MIDI bridge"
    if [ "$FEEDBACK_TO_DRIVER" = "1" ]; then
        "$BRIDGE" --poll-ms "$POLL_MS" --feedback-log "$FEEDBACK_LOG"
    else
        "$BRIDGE" --poll-ms "$POLL_MS" --no-driver-feedback --feedback-log "$FEEDBACK_LOG"
    fi

    status=$?
    log "MIDI bridge exited with status $status; restarting after delay"
    sleep 2
done
