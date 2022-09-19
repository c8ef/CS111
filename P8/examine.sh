#!/bin/sh

# This script dumps all the live blocks and inodes in a V6 file system.
# With the -a flag, makes a copy and runs ./apply first.

export TZ=US/Pacific

usage() {
    echo "usage: $(basename $0) [-a] file-system.img" >&2
    exit 1
}

run_apply=
if test 2 = "$#" -a x"$1" = x-a; then
    shift
    run_apply=1
fi

if test 1 != "$#"; then
    usage
fi

if test "$run_apply"; then
    tmp=$(mktemp XXXXXXXXXX.img)
    trap "rm $tmp" 0
    cp "$1" "$tmp"
    if ! ./apply "$tmp"; then
	echo "./apply failed" >&2
	exit 1
    fi
    export V6IMG="$tmp"
else
    export V6IMG="$1"
fi

if ./fsckv6 "$V6IMG"; then
    ./v6 usedblocks | sed -e 1q
    ./v6 block $(./v6 usedblocks | sed -e 1d)
    ./v6 usedinodes | sed -e 1q
    for inode in $(./v6 usedinodes | sed -e 1d); do
	      ./v6 stat "#$inode"
    done
fi
