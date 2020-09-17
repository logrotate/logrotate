#!/bin/sh

. ./test-common.sh

cleanup 31

# ------------------------------- Test 31 ------------------------------------
# Test mode in create option
preptest test.log 31 1 0

$RLR test-config.31 --force

$STAT_MODE_FORMAT test.log|grep 8180 >/dev/null
if [ $? != 0 ]; then
	echo "Bad mode of test.log, should be 0600, is:"
	$STAT_MODE_FORMAT test.log
	exit 3
fi

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
