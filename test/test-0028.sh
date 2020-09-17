#!/bin/sh

. ./test-common.sh

cleanup 28

# ------------------------------- Test 28 ------------------------------------
# { on new line

preptest test.log 28 1 0
# log with 1 byte should not be rotated
$RLR test-config.28

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
