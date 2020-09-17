#!/bin/sh

. ./test-common.sh

cleanup 16

# ------------------------------- Test 16 ------------------------------------
preptest test.log 16 1 0
# log with 1 byte should not be rotated
echo "a" > test.log
$RLR test-config.16

if [ -f test.log.1 ]; then
	echo "file $file does exist!"
	exit 2
fi

# log with 4 bytes should be rotated
echo "zero" > test.log
$RLR test-config.16

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
