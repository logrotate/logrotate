#!/bin/sh

. ./test-common.sh

cleanup 29

# ------------------------------- Test 29 ------------------------------------
# { } on the same line

preptest test.log 29 1 0
# log with 1 byte should not be rotated
$RLR test-config.29 --force

checkoutput <<EOF
test.log 0
test.log.1 0 zero
EOF
