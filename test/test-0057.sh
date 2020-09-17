#!/bin/sh

. ./test-common.sh

cleanup 57

# ------------------------------- Test 57 ------------------------------------
# When compressing program prints something to stderr, we should prepend it
# with the log name.
preptest test.log 57 1
$RLR test-config.57 --force 2>error.log

grep "error: Compressing" error.log >/dev/null
if [ $? != 0 ]; then
	cat error.log
	echo "No error printed, but there should be one."
	exit 3
fi

grep "compression error" error.log >/dev/null
if [ $? != 0 ]; then
	cat error.log
	echo "No error printed, but there should be one."
	exit 3
fi

rm -f error.log

checkoutput <<EOF
test.log 0
test.log.1.gz 1 zero
EOF
