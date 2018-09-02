#!/bin/bash

# THIS IS NOT SUPPOSED TO BE A THOROUGH WAY TO TEST YOUR SYSTEM.
# Do not rely on this for full correctness; but this may be useful.
# Use it to your own discretion.

set -e

SYS161=sys161
DISK161=disk161

MKSFS=hostbin/host-mksfs
DUMPSFS=hostbin/host-dumpsfs
SFSCK=hostbin/host-sfsck
POISONDISK=hostbin/host-poisondisk

DISK=LHD1.img

usage() {
	echo "usage: $0 DOOM WORKLOAD [OPTIONS]" >&2
}

if [ -z "$1" ] || [ -z "$2" ]; then
	usage
	exit 2
fi

DOOM="$1"
shift

echo "$DOOM"

rm -f "$DISK"
"$DISK161" create "$DISK" 30M
"$POISONDISK" "$DISK"
"$MKSFS" "$DISK" $(basename "$DISK" .img)
"$SFSCK" "$DISK"
./testscripts/test.py --doom="$DOOM" "mount sfs lhd1:;bootfs lhd1:;p emu0:bin/sh;emu0:testbin/frack do $*;exit;exit"||true
"$DUMPSFS" -j "$DISK" > frack.dump || true
"$DUMPSFS" -a "$DISK" >> frack.dump || true
"$DUMPSFS" -df -i 2 "$DISK" >> frack.homeless || true
printf "Continue? " >&2
read
#./testscripts/test.py "mount sfs lhd1:; exit;"
./testscripts/test.py "mount sfs lhd1:;bootfs lhd1:;p emu0:bin/sh;emu0:testbin/frack check $*;exit;exit"

"$SFSCK" "$DISK"
