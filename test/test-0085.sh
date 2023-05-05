#!/bin/sh

. ./test-common.sh

cleanup 85

# ------------------------------- Test 85 ------------------------------------
preptest test.log 85 9

$RLR test-config.85 -f || exit 23

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test.log.2 0 first
test.log.3 0 second
test.log.4 0 third
test.log.5 0 fourth
test.log.6 0 fifth
test.log.7 0 sixth
test.log.8 0 seventh
test.log.9 0 eighth
EOF

# Set log modification time to some date in the past
touch -t 200001010000 test.log.1  # 1 Jan 2000
touch -t "$($DATE_DATEARG @$(($(date +%s) - 12 * 60 * 60)) +%Y%m%d%H%M)" test.log.2  # -12H
touch -t "$($DATE_DATEARG @$(($(date +%s) - 23 * 60 * 60)) +%Y%m%d%H%M)" test.log.3  # -23H
touch -t "$($DATE_DATEARG @$(($(date +%s) - 24 * 60 * 60)) +%Y%m%d%H%M)" test.log.4  # -24H
touch -t "$($DATE_DATEARG @$(($(date +%s) - 25 * 60 * 60)) +%Y%m%d%H%M)" test.log.5  # -25H
touch -t "$($DATE_DATEARG @$(($(date +%s) - 36 * 60 * 60)) +%Y%m%d%H%M)" test.log.6  # -36H
touch -t "$($DATE_DATEARG @$(($(date +%s) - 47 * 60 * 60)) +%Y%m%d%H%M)" test.log.7  # -47H
touch -t "$($DATE_DATEARG @$(($(date +%s) - 48 * 60 * 60)) +%Y%m%d%H%M)" test.log.8  # -48H
touch -t "$($DATE_DATEARG @$(($(date +%s) - 49 * 60 * 60)) +%Y%m%d%H%M)" test.log.9  # -49H

echo "content" >> test.log

$RLR test-config.85 -f || exit 23

# maxage should remove this file
if [ -f test.log.2 ]; then
    echo "test.log.2 not removed" >&2
    exit 3
fi

# maxage should remove this file
if [ -f test.log.9 ]; then
    echo "test.log.9 not removed" >&2
    exit 3
fi

# should never be created
if [ -f test.log.10 ]; then
    echo "test.log.10 exists" >&2
    exit 3
fi

checkoutput <<EOF
test.log 0
test.log.1 0 content
test.log.3 0 first
test.log.4 0 second
test.log.5 0 third
test.log.6 0 fourth
test.log.7 0 fifth
test.log.8 0 sixth
EOF
