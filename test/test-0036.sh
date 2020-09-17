#!/bin/sh

. ./test-common.sh

cleanup 36

# ------------------------------- Test 36 ------------------------------------
# size 1x - 'x' is unknown unit, config should be skipped
preptest test.log 36 1 0

$RLR test-config.36 --force 2>error.log

grep "unknown unit" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error 'unknown unit' printed, but there should be one."
	exit 3
fi

checkoutput <<EOF
test.log 0 zero
EOF
