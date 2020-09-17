#!/bin/sh

. ./test-common.sh

cleanup 43

# ------------------------------- Test 43 ------------------------------------
# Test that prerotate and postrotate scripts are called twice for two files
# when sharedscripts defined
preptest test.log 43 1
echo number2 > test2.log

$RLR test-config.43

# Check both possible orders
grep "test2.log;test2.log;test.log;test.log;" scriptout >/dev/null
if [ $? != 0 ]; then
	grep "test.log;test.log;test2.log;test2.log;" scriptout >/dev/null
	if [ $? != 0 ]; then
		echo "ERROR: scriptout should contain 'test2.log;test2.log;test.log;test.log;' or 'test.log;test.log;test2.log;test2.log;'"
		exit 3
	fi
fi

rm -f scriptout

checkoutput <<EOF
test.log 0
test.log.1 0 zero
test2.log 0
test2.log.1 0 number2
EOF
