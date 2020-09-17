#!/bin/sh

. ./test-common.sh

cleanup 26

# ------------------------------- Test 26 ------------------------------------
# If there is error in config file, log should not be rotated and original log
# should be untouched

preptest test.log 26 1 0
# log with 1 byte should not be rotated
$RLR test-config.26 2>error.log

grep "unknown option" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi

rm error.log

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
