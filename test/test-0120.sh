#!/bin/sh

. ./test-common.sh

cleanup 120

# ------------------------------- Test 120 -----------------------------------
# A 'minsize' value greater than the 'maxsize' value, possibly inherited from
# the global configuration, should be reported with a warning, also in debug
# mode. The log definition itself must still be processed.

preptest test.log 120 1 0
createlog 0 anothertest.log
createlog 0 differenttest.log

$RLR -d test-config.120 2>error.log || exit 23

if ! grep -q "warning: test-config.120:9 minsize (20) is greater than maxsize (10) for $PWD/test.log" error.log; then
	echo "No warning printed in debug mode, but there should be one."
	exit 3
fi

$RLR --force test-config.120 2>error.log || exit 23

if ! grep -q "warning: test-config.120:9 minsize (20) is greater than maxsize (10) for $PWD/test.log" error.log; then
	echo "No warning printed, but there should be one."
	exit 3
fi

if [ "$(grep -c 'is greater than maxsize' error.log)" != 1 ]; then
	echo "Unexpected warning printed for a non-conflicting definition."
	exit 3
fi

rm error.log

checkoutput <<EOF
test.log 0
test.log.1 0 zero
anothertest.log 0
anothertest.log.1 0 zero
differenttest.log 0
differenttest.log.1 0 zero
EOF
