#!/bin/sh

. ./test-common.sh

cleanup 117

# ------------------------------- Test 117 -----------------------------------
# A 'dateformat' directive without a value should be rejected with an error
# and only that log definition should be skipped. Check both unindented and
# indented closing braces, with a valid log definition after each error.

preptest test.log 117 1 0
createlog 0 anothertest.log
createlog 0 differenttest.log
createlog 0 differentgood.log
$RLR --force test-config.117 2>error.log && exit 23

grep "argument expected after dateformat" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi

rm error.log

checkoutput <<EOF
test.log 0 zero
anothertest.log 0
anothertest.log.1 0 zero
differenttest.log 0 zero
differentgood.log 0
differentgood.log.1 0 zero
EOF
