#!/bin/sh

. ./test-common.sh

# check state file locking
cleanup 87

preptest test.log 87 1

touch state

$RLR test-config.87 -f &

sleep 2

$RLR test-config.87
ret=$?

if [ $ret -ne 3 ]; then
	echo "Expected second instance to fail (returned $ret - expected 3)."
	exit 3
fi
