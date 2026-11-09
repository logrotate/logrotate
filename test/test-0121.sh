#!/bin/sh

. ./test-common.sh

cleanup 121

# ------------------------------- Test 121 -----------------------------------
# With "rotate -1" only rotated logs with an index in canonical decimal form
# are shifted, the highest existing index is not overwritten with "start 0",
# and an index that cannot be shifted without overflow aborts the rotation.
preptest test.log 121 1

echo before > test.log.0
echo leading > test.log.05
echo sign > test.log.+3
echo range > test.log.99999999999

$RLR test-config.121 --force || exit 23

checkoutput <<EOF2
test.log 0
test.log.0 0 zero
test.log.1 0 before
test.log.2 0 first
test.log.05 0 leading
test.log.+3 0 sign
test.log.99999999999 0 range
EOF2

if [ -e test.log.3 ]; then
    echo "unexpected file test.log.3" >&2
    exit 3
fi

echo max > test.log.2147483647

$RLR test-config.121 --force 2>error.log && exit 23

grep "index of rotated log .*test.log.2147483647 is too large" error.log >/dev/null
if [ $? != 0 ]; then
    echo "No error printed, but there should be one."
    exit 3
fi

rm error.log

checkoutput <<EOF2
test.log 0
test.log.0 0 zero
test.log.1 0 before
test.log.2 0 first
test.log.2147483647 0 max
EOF2

if [ -e test.log.3 ] || [ -e test.log.-2147483648 ]; then
    echo "unexpected rotation" >&2
    exit 3
fi
