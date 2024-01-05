#!/bin/sh

. ./test-common.sh

cleanup 110

# ------------------------------- Test 110 ------------------------------------
# Does "create 1000 1001" mean "mode 01000, owner 1001", or "owner 1000, group 1001"?

preptest test1.log 110 1
preptest test2.log 110 1
preptest test3.log 110 1
preptest test4.log 110 1

$RLR test-config.110 --force 2>&1 | tee output.log

if ! grep -qE 'test1.log mode = 0755 uid = 1 gid = 2' output.log; then
	echo "\"create 0755 1001 1002\" should mean mode 0755 numeric UID 1001 numeric GID 1002"
	exit 3
fi

if ! grep -qE 'test2.log mode = [0-9]+ uid = 1 gid = 2' output.log; then
	echo "\"create 1001 1002\" should mean numeric UID 1001 numeric GID 1002"
	exit 3
fi

if ! grep -qE 'test3.log mode = 0700 uid = [0-9]+ gid = [0-9]+' output.log; then
	echo "\"create 0700\" should mean mode 0700"
	exit 3
fi

if ! grep -q "error: test-config.110:22 unknown group 'bar baz'" output.log; then
	echo "\"su 'foo bar'\" should mean user 'foo bar'"
	exit 3
fi
