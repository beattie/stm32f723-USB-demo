#!/bin/bash
# Flash and reset STM32F723, retrying on probe glitches
ELF="${1:-$HOME/STM32F723_bringup/build/STM32F723_bringup.elf}"
CHIP="STM32F723VETx"
PROBE="1fc9:0090"

for attempt in 1 2 3 4 5; do
    echo "Flash attempt $attempt..."
    if probe-rs download --chip "$CHIP" --probe "$PROBE" "$ELF" && \
       probe-rs reset  --chip "$CHIP" --probe "$PROBE"; then
        echo "Done."
        exit 0
    fi
    echo "Probe error, retrying in 3s..."
    sleep 3
done
echo "Failed after 5 attempts — replug LPC-LINK2 and try again."
exit 1
