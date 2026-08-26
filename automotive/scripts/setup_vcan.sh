#!/usr/bin/env bash
#
# Bring up a virtual CAN interface so the whole CAN chain can be exercised
# without any hardware. Needed from week 4 onward.
#
#   sudo ./scripts/setup_vcan.sh          # create vcan0
#   sudo ./scripts/setup_vcan.sh vcan1    # create a second bus
#
# vcan is not persistent: rerun after every reboot.
#
# Verify with:  ip -details link show vcan0
# Watch with:   candump vcan0          (sudo apt install can-utils)

set -euo pipefail

readonly kInterface="${1:-vcan0}"

if [[ "${EUID}" -ne 0 ]]; then
    echo "error: needs root (modprobe + ip link). Rerun with sudo." >&2
    exit 1
fi

if ! lsmod | grep -q '^vcan'; then
    echo "loading vcan kernel module"
    modprobe vcan
fi

if ip link show "${kInterface}" >/dev/null 2>&1; then
    echo "${kInterface} already exists"
else
    echo "creating ${kInterface}"
    ip link add dev "${kInterface}" type vcan
fi

ip link set up "${kInterface}"

echo
ip -details -brief link show "${kInterface}"
echo
echo "OK - ${kInterface} is up"
