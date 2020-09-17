#!/bin/sh

. ./test-common.sh

cleanup 38

# ------------------------------- Test 38 ------------------------------------
# preremove script
preptest test.log 38 1
preptest test2.log 38 1
$RLR test-config.38 --force

# Check both possible orders
grep "test2.log.1test.log.1" scriptout >/dev/null
if [ $? != 0 ]; then
	grep "test.log.1test2.log.1" scriptout >/dev/null
	if [ $? != 0 ]; then
		echo "ERROR: scriptout should contain 'test2.log.1test.log.1' or 'test.log.1test2.log.1'"
		exit 3
	fi
fi

rm -f scriptout

checkoutput <<EOF
test.log 0
test2.log 0
EOF
