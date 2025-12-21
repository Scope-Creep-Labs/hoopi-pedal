#!/bin/bash
# Wait for Daisy to appear in DFU mode and program it

TIMEOUT=10

echo "Waiting for Daisy DFU device..."
echo "Power on or reset the Daisy now."

for i in $(seq 1 $TIMEOUT); do
    if dfu-util --list 2>&1 | grep -q "0483:df11"; then
        echo ""
        echo "DFU device detected!"
        sleep 0.3  # Brief settle time
        make program-dfu
        exit $?
    fi
    sleep 1
    echo -n "."
done

echo ""
echo "Timeout: DFU device not found after ${TIMEOUT}s"
echo ""
echo "Debug: run 'dfu-util --list' while device is in DFU mode"
exit 1
