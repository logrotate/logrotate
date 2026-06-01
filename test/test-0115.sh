#!/bin/sh

. ./test-common.sh

cleanup 115

# ------------------------------- Test 115 -----------------------------------
# A 'dateformat' directive without a value should be rejected with an error
# and the config file should be skipped

preptest test.log 115 1 0
$RLR test-config.115 2>error.log && exit 23

grep "argument expected after dateformat" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi

rm error.log

checkoutput <<EOF
test.log 0 zero
EOF
