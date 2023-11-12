#!/bin/sh

. ./test-common.sh

# Does "create 1000 1001" mean "mode 01000, owner 1001", or "owner 1000, group 1001"?
cleanup 95

# ------------------------------- Test 95 ------------------------------------
preptest test.log 95 1 0
rm -rf testdir

$RLR test-config.95 --force 2>&1 | tee output.log

if ! grep -q 'uid = 1001' output.log; then
	echo "\"create 1001 1002\" could mean numeric UID 1001 numeric GID 1002, maybe"
	exit 3
fi
