#!/bin/sh

. ./test-common.sh

cleanup 44

# ------------------------------- Test 44 ------------------------------------
# Test that prerotate and postrotate scripts are called once when nosharedscripts
# is defined and one rotation fails
preptest test.log 44 1

$RLR test-config.44 2>error.log

grep "error: stat of" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi

rm -f error.log

checkoutput <<EOF
test.log 0
test.log.1 0 zero
scriptout 0 "test.log;test.log;"
EOF
