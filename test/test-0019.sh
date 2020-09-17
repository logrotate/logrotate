#!/bin/sh

. ./test-common.sh

cleanup 19

# ------------------------------- Test 19 ------------------------------------
preptest test.log 19 1
$RLR test-config.19 --force 2>error.log
if [ $? = 0 ]; then
	echo "Logrotate exited with 0 exit code, but it should not"
fi

grep "error running non-shared postrotate script for" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi
