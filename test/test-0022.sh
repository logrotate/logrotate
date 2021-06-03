#!/bin/sh

. ./test-common.sh

cleanup 22

# ------------------------------- Test 22 ------------------------------------
# different base name, so it should not find the file
preptest differenttest.log 22 1
$RLR test-config.22 --force 2>error.log && exit 23

grep "error: stat of" error.log >/dev/null
if [ $? != 0 ]; then
	echo "No error printed, but there should be one."
	exit 3
fi
