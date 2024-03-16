#!/bin/sh

. ./test-common.sh

# what is the default mode of a new old directory?
cleanup 94

# ------------------------------- Test 94 ------------------------------------
preptest test.log 94 1 0
rm -rf testdir

$RLR test-config.94 --force 2>&1 | tee output.log

if grep -q 'mode = 037777777777' output.log; then
	echo "creating directory with mode -1 is not good"
	exit 3
fi
