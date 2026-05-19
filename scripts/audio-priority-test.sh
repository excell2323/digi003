#!/bin/sh
set -eu

echo "Audio-critical processes:"
ps -axo pid,ni,pri,pcpu,comm |
    egrep 'FireWireOHCIProbe|coreaudiod|digi003-midi-bridge|Pro Tools|V-Control|VCPMidiServer' |
    egrep -v egrep || true

echo ""
echo "Lowering non-critical helpers for this login session..."
for pattern in \
    'SoundFlow Helper' \
    'SoundFlow.app/Contents/Helpers' \
    'Codex Helper' \
    'Aktivitätsanzeige'; do
    pgrep -f "$pattern" | while IFS= read -r pid; do
        [ -n "$pid" ] || continue
        renice 10 -p "$pid" >/dev/null 2>&1 || true
        taskpolicy -b -p "$pid" >/dev/null 2>&1 || true
    done
done

echo ""
echo "If you want to boost audio processes above nice 0, run with admin:"
printf '  sudo renice -10 -p '
pgrep -f 'FireWireOHCIProbe|coreaudiod|digi003-midi-bridge|Pro Tools|V-Control Pro|VCPMidiServer' |
    tr '\n' ' '
echo

echo ""
echo "Current process snapshot:"
ps -axo pid,ni,pri,pcpu,comm |
    egrep 'FireWireOHCIProbe|coreaudiod|digi003-midi-bridge|Pro Tools|V-Control|VCPMidiServer|SoundFlow|Codex Helper|Aktivitätsanzeige' |
    egrep -v egrep || true
