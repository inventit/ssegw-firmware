# !/bin/sh

[ -f firmware.conf ] || (echo "firmware.conf was not found."; exit 1)
. firmware.conf

expected_version=${VERSION}

cat /proc/sys/kernel/version | grep -e ${expected_version}
