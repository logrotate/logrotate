#!/bin/sh

. ./test-common.sh

cleanup 45

# ------------------------------- Test 45 ------------------------------------
# Test that prerotate and postrotate scripts are not called when sharedscripts
# is defined and one rotation fails
preptest test.log 45 1

touch scriptout
$RLR test-config.45 2>error.log

grep "error: stat of" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi

rm -f error.log

checkoutput <<EOF
test.log 0 zero
scriptout 0
EOF
