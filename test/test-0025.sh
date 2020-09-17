#!/bin/sh

. ./test-common.sh

cleanup 25

# ------------------------------- Test 25 ------------------------------------
# If there is no '{' character after log files definition, error should be printed
# and config file should be skipped

preptest test.log 25 1 0
# log with 1 byte should not be rotated
$RLR test-config.25 2>error.log

grep "missing '{' after log files definition" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi

rm error.log

checkoutput <<EOF
test.log 0 zero
EOF
