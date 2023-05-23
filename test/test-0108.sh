#!/bin/sh

. ./test-common.sh

cleanup 108

# ------------------------------- Test 108 ------------------------------------
# test setting atime and time on compressed files
preptest test.log 108 0

checkoutput <<EOF
test.log 0 zero
EOF

# Set log modification time to some date in the past
TZ=UTC0 touch -t 200001010000 test.log

$RLR test-config.108 --force || exit 23

atime=$($STAT_ATIME_FORMAT test.log.1.gz)
mtime=$($STAT_MTIME_FORMAT test.log.1.gz)
expected_time=946684800

if [ "$atime" -ne $expected_time ]; then
    echo "atime not set (expected $expected_time, got $atime)" >&2
    exit 3
fi

if [ "$mtime" -ne $expected_time ]; then
    echo "mtime not set (expected $expected_time, got $mtime)" >&2
    exit 3
fi

# check last, to not modify atime
checkoutput <<EOF
test.log 0
test.log.1.gz 1 zero
EOF
