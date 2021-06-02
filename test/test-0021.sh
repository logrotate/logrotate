#!/bin/sh

. ./test-common.sh

cleanup 21

# ------------------------------- Test 21 ------------------------------------
# different base name, so it should not find the file
preptest differenttest.log 21 1
$RLR test-config.21 --force 2>error.log || exit 23

cat error.log

# grep "error running shared postrotate script for" error.log >/dev/null
# if [ $? != 0 ]; then
# 	echo "No error printed, but there should be one."
# 	exit 3
# fi
