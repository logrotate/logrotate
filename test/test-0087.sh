#!/bin/sh

. ./test-common.sh

# check state file locking
cleanup 87

preptest test.log 87 1

touch state
chmod 0640 state

$RLR test-config.87 -f &
process_id=$!

sleep 2

$RLR test-config.87
ret=$?

wait $process_id || exit $?

if [ $ret -ne 3 ]; then
	echo "Expected second instance to fail (returned $ret - expected 3)."
	exit 3
fi
