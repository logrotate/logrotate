#!/bin/sh

. ./test-common.sh

cleanup 126

# ------------------------------- Test 126 -----------------------------------
# A number in the state file which is out of the range of int must be
# rejected instead of being truncated, e.g. 4294969296 into 2000.
preptest test.log 126 0

cat > state <<EOF2
logrotate state -- version 2
"$PWD/test.log" 4294969296-1-1
EOF2

$RLR test-config.126 2>error.log && exit 23

grep "bad line 2 in state file" error.log >/dev/null
if [ $? != 0 ]; then
    echo "No error printed, but there should be one."
    exit 3
fi

rm error.log

# A valid state file with all fields is still accepted
cat > state <<EOF2
logrotate state -- version 2
"$PWD/test.log" 2000-1-1-0:0:0
EOF2

$RLR test-config.126 || exit 23

checkoutput <<EOF2
test.log 0
test.log.1 0 zero
EOF2
