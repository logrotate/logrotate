#!/bin/sh

. ./test-common.sh

cleanup 20

# ------------------------------- Test 20 ------------------------------------
preptest test.log 20 1
$RLR test-config.20 --force 2>error.log && exit 23

grep "error running shared postrotate script for" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi
