#!/bin/sh

. ./test-common.sh

cleanup 131

# ------------------------------- Test 131 ------------------------------------
# A missing olddir without createolddir is still an error in debug mode.
preptest test.log 131 1

$RLR test-config.131 --force -d 2>error.log && exit 23

if ! grep -q "error verifying olddir path .*/testdir: " error.log; then
	echo "no error in debug mode for the missing olddir"
	exit 3
fi

rm error.log
