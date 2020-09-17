#!/bin/sh

. ./test-common.sh

cleanup 17

# ------------------------------- Test 17 ------------------------------------
preptest test.log 17 1 0
# log with 1 byte should not be rotated
$RLR test-config.17 -l logrotate.log 2>error.log

grep "unexpected } (missing previous '{')" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi

rm error.log

grep "reading config file test-config.17" logrotate.log >/dev/null
if [ $? != 0 ]; then
	echo "There is no log output in logrotate.log"
	exit 3
fi

rm -f logrotate.log

checkoutput <<EOF
test.log 0 zero
EOF
