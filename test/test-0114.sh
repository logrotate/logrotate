#!/bin/sh

. ./test-common.sh

cleanup 114

# ------------------------------- Test 114 -----------------------------------
# If a log files definition block is not closed with a '}' before the end of
# the config file, an error should be printed and the config file skipped

preptest test.log 114 1 0
$RLR test-config.114 2>error.log && exit 23

grep "unexpected end of file, missing '}' after log file definition" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi

rm error.log

checkoutput <<EOF
test.log 0 zero
EOF
