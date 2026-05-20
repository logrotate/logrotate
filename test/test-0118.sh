#!/bin/sh

. ./test-common.sh

cleanup 118

# ------------------------------- Test 118 -----------------------------------
# A brace at the end of an invalid option value must not close the block.
# Skip each invalid definition and rotate the valid definition following it.

preptest test.log 118 1 0
createlog 0 anothertest.log
createlog 0 differenttest.log
createlog 0 differentgood.log
$RLR --force test-config.118 2>error.log && exit 23

grep "unknown unit '}'" error.log >/dev/null || exit 3
grep "bad rotation count '1 # }'" error.log >/dev/null || exit 3
if grep "unexpected }" error.log >/dev/null; then
	echo "An option value was mistaken for a closing brace."
	exit 3
fi

rm error.log

checkoutput <<EOF
test.log 0 zero
anothertest.log 0
anothertest.log.1 0 zero
differenttest.log 0 zero
differentgood.log 0
differentgood.log.1 0 zero
EOF
