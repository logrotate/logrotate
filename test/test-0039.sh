#!/bin/sh

. ./test-common.sh

cleanup 39

# ------------------------------- Test 39 ------------------------------------
# preremove script error - do not remove log file
preptest test.log 39 1
preptest test2.log 39 1
$RLR test-config.39 --force 2>error.log

grep "error running preremove script" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error 'error running preremove script' printed, but there should be one."
	exit 3
fi

# Check both possible orders
grep "test2.log.1" scriptout >/dev/null
if [ $? != 0 ]; then
	grep "test.log.1" scriptout >/dev/null
	if [ $? != 0 ]; then
		echo "ERROR: scriptout should contain 'test2.log.1' or 'test.log.1'"
		exit 3
	fi
fi

rm -f scriptout

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test2.log 0
test2.log.1 0 zero
EOF
