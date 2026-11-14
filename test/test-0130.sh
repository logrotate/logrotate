#!/bin/sh

. ./test-common.sh

cleanup 130

# ------------------------------- Test 130 ------------------------------------
# In debug mode a missing olddir must not be created, even with createolddir,
# and the lookups of rotated logs in the missing olddir must not print errors.
# A later section that uses the olddir without createolddir must still pass.
# The same configuration creates the olddir without debug mode.
preptest test.log 130 1 0
createlog 0 anothertest.log
createlog 0 test-same.log
rm -rf testdir

$RLR test-config.130 --force -d 2>error.log || exit 23

if [ -e testdir ]; then
	echo "olddir was created in debug mode"
	exit 3
fi

if grep -q '^error:' error.log; then
	echo "error printed in debug mode for the missing olddir:"
	grep '^error:' error.log
	exit 3
fi

rm error.log

$RLR test-config.130 --force || exit 23

DATESTRING=$(/bin/date +%Y%m%d)

checkoutput <<EOF
test.log 0
testdir/sub/test.log.1 0 zero
anothertest.log 0
testdir/sub/anothertest.log-$DATESTRING 0 zero
test-same.log 0
testdir/sub/test-same.log.1 0 zero
EOF

rm -rf testdir
