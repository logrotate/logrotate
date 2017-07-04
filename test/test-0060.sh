#!/bin/bash

. ./test-common.sh

cleanup 60

# ------------------------------- Test 60 ------------------------------------
# Test we log debug output using -l option when passed.
preptest test.log 61 1 0

$RLR test-config.61 --force -l ./logrotate.log

DATESTRING=$(/bin/date +%Y-%m-%d-%H)

grep "reading config file test-config.61" logrotate.log >/dev/null
if [ $? != 0 ]; then
	echo "There is no log output in logrotate.log"
	exit 3
fi

rm -f logrotate.log

checkoutput <<EOF
test.log 0
test.log.$DATESTRING 0 zero
EOF
