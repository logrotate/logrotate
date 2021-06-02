#!/bin/sh

. ./test-common.sh

cleanup 19

# ------------------------------- Test 19 ------------------------------------
preptest test.log 19 1
$RLR test-config.19 --force 2>error.log && exit 23

grep "error running non-shared postrotate script for" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi
